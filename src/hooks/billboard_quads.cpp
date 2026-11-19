// Upright billboards that face the camera. Some map models are a single
// upright quad drawn as an NNS G3D Y-axis billboard (BBY), such as the
// standing torches in churches. The billboard keeps the model's own axes
// facing the camera, but the quad in some of these models is turned about
// its vertical axis in the model data itself (about 22 degrees for the
// torches). The DS draws it that way too: the sprite stands slanted, with
// one side higher and farther away than the other. Before such a shape is
// drawn, the hook turns the quad's vertices in the model data so that it
// lies in the model's XY plane, facing the camera like the other sprites.
// See config/hooks/dq9_arm9.toml for the guest code.
#include <cmath>
#include <cstdint>

#include "title_hooks.h"

namespace {

// Set by the BBY node command, used by the shape command that follows it.
bool g_billboard_pending = false;

// Parameter words of each geometry command (GBATEK), -1 for the ones a
// shape's display list should not hold.
int ParamCount(uint8_t op) {
    switch (op) {
        case 0x00: case 0x11: case 0x15: case 0x41: return 0;
        case 0x10: case 0x12: case 0x13: case 0x14: return 1;
        case 0x16: case 0x18: return 16;
        case 0x17: case 0x19: return 12;
        case 0x1A: return 9;
        case 0x1B: case 0x1C: return 3;
        case 0x20: case 0x21: case 0x22: case 0x24: case 0x25: case 0x26:
        case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x30:
        case 0x31: case 0x32: case 0x33: case 0x40: return 1;
        case 0x23: return 2;
        case 0x34: return 32;
        default: return -1;
    }
}

int32_t Low16(uint32_t word) { return static_cast<int16_t>(word & 0xFFFFu); }
int32_t High16(uint32_t word) { return static_cast<int16_t>(word >> 16); }

uint32_t Pack16(int32_t low, int32_t high) {
    return (static_cast<uint32_t>(high) << 16) | (static_cast<uint32_t>(low) & 0xFFFFu);
}

struct QuadVertex {
    uint8_t op;      // 0x23 VTX_16, 0x25 VTX_XY, 0x26 VTX_XZ, 0x27 VTX_YZ
    uint32_t param;  // address of its first parameter word
    int32_t x, y, z;
};

// The display list's one quad, if that is all it draws and its first vertex
// is absolute (VTX_16), so every coordinate is known.
bool ReadQuad(uint32_t dl, uint32_t size, QuadVertex (&quad)[4]) {
    if (size == 0u || size > 0x400u || (dl & 3u) != 0u ||
        dl - 0x02000000u >= 0x00400000u)
        return false;
    const uint32_t end = dl + size;
    int begins = 0;
    int count = 0;
    bool known = false;
    int32_t x = 0, y = 0, z = 0;
    uint32_t p = dl;
    while (p < end) {
        const uint32_t ops = bus_read_u32(p);
        p += 4u;
        for (int k = 0; k < 4; ++k) {
            const uint8_t op = static_cast<uint8_t>(ops >> (8 * k));
            const int n = ParamCount(op);
            if (n < 0 || (op >= 0x10 && op <= 0x1C)) return false;  // matrices
            if (p + 4u * static_cast<uint32_t>(n) > end) return false;
            const uint32_t w = n > 0 ? bus_read_u32(p) : 0u;
            switch (op) {
                case 0x40:
                    if ((w & 3u) != 1u || ++begins > 1) return false;  // quads
                    break;
                case 0x24: case 0x28:
                    return false;  // VTX_10, VTX_DIFF
                case 0x23:
                    x = Low16(w);
                    y = High16(w);
                    z = Low16(bus_read_u32(p + 4u));
                    known = true;
                    break;
                case 0x25: x = Low16(w); y = High16(w); break;
                case 0x26: x = Low16(w); z = High16(w); break;
                case 0x27: y = Low16(w); z = High16(w); break;
                default: break;
            }
            if (op >= 0x23 && op <= 0x27) {
                if (!known || begins != 1 || count >= 4) return false;
                quad[count++] = {op, p, x, y, z};
            }
            p += 4u * static_cast<uint32_t>(n);
        }
    }
    return count == 4;
}

// Turned less than this, or more (a quad seen from the side), it is left
// alone.
constexpr double kDegree = 3.14159265358979323846 / 180.0;
constexpr double kMinTurn = 0.5 * kDegree;
constexpr double kMaxTurn = 60.0 * kDegree;

void StraightenQuad(uint32_t dl, uint32_t size) {
    QuadVertex quad[4];
    if (!ReadQuad(dl, size, quad)) return;

    // An upright quad: two vertical edges, each two vertices at one (x, z).
    const QuadVertex& a = quad[0];
    int other = -1;
    int same = 0;
    for (int i = 1; i < 4; ++i) {
        if (quad[i].x == a.x && quad[i].z == a.z) {
            ++same;
        } else if (other < 0) {
            other = i;
        } else if (quad[i].x != quad[other].x || quad[i].z != quad[other].z) {
            return;
        }
    }
    if (same != 1 || other < 0) return;
    const QuadVertex& b = quad[other];
    const double dx = double(b.x) - a.x;
    const double dz = double(b.z) - a.z;
    if (dz == 0 || dx == 0) return;
    const double turn = std::atan(dz / dx);
    if (std::fabs(turn) < kMinTurn || std::fabs(turn) > kMaxTurn) return;

    // Turn both edges about the quad's middle, into the plane z = middle.
    const double cx = (double(a.x) + b.x) / 2;
    const double cz = (double(a.z) + b.z) / 2;
    const double c = std::cos(-turn), s = std::sin(-turn);
    int32_t new_x[2], new_z = static_cast<int32_t>(std::lround(cz));
    const double ex[2] = {a.x - cx, b.x - cx};
    const double ez[2] = {a.z - cz, b.z - cz};
    for (int e = 0; e < 2; ++e) {
        // Rotation about Y as the geometry engine applies it to (x, z).
        new_x[e] = static_cast<int32_t>(std::lround(cx + ex[e] * c - ez[e] * s));
        if (new_x[e] < -32768 || new_x[e] > 32767) return;
    }
    if (new_z < -32768 || new_z > 32767) return;

    for (QuadVertex& v : quad) {
        const int32_t x = new_x[(v.x == a.x && v.z == a.z) ? 0 : 1];
        switch (v.op) {
            case 0x23:
                bus_write_u32(v.param, Pack16(x, v.y));
                bus_write_u32(v.param + 4u,
                              (bus_read_u32(v.param + 4u) & 0xFFFF0000u) |
                                  (static_cast<uint32_t>(new_z) & 0xFFFFu));
                break;
            case 0x25: bus_write_u32(v.param, Pack16(x, v.y)); break;
            case 0x26: bus_write_u32(v.param, Pack16(x, new_z)); break;
            case 0x27: bus_write_u32(v.param, Pack16(v.y, new_z)); break;
            default: break;
        }
    }
}

}  // namespace

// Entry of NNS G3D's BBY node command (0x020B5404, in the command table at
// 0x020F1E28).
NDS_TITLE_HOOK void Dq9BillboardYNode() {
    g_billboard_pending = true;
}

// Entry of NNS G3D's shape command (0x020B4BFC, r0 = render state): the
// shape after a BBY node is its billboard.
NDS_TITLE_HOOK void Dq9BillboardShape(uint32_t* r0) {
    if (!g_billboard_pending) return;
    g_billboard_pending = false;
    const uint32_t rs = *r0;
    // The shape command's own tests: drawn, with a material, not skipped.
    const uint32_t flag = bus_read_u32(rs + 8u);
    if ((flag & 0x200u) != 0u || (flag & 1u) == 0u || (flag & 2u) != 0u) return;
    // The shape dictionary (rs + 0xDC) and the command's shape index.
    const uint32_t dict = bus_read_u32(rs + 0xDCu);
    if (dict - 0x02000000u >= 0x00400000u) return;
    const uint32_t index = bus_read_u8(bus_read_u32(rs) + 1u);
    if (index >= bus_read_u8(dict + 1u)) return;
    const uint32_t data = dict + bus_read_u16(dict + 6u);
    const uint32_t entry = data + 4u + bus_read_u16(data) * index;
    const uint32_t shape = dict + bus_read_u32(entry);
    // NNSG3dResShp: tag, size, flag, then the display list's offset and size.
    StraightenQuad(shape + bus_read_u32(shape + 8u), bus_read_u32(shape + 12u));
}
