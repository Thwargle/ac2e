"""Validate capture visuals against this installation, not just DID presence.

The retained captures span different client builds. A DID existing in portal.dat
does not mean it still describes the captured model. Unverified observations
stay in the source manifest and are reported for later recovery.
"""
from population_probe import Reader, entity
from dat_probe import Archive


class ContentProfile:
    def __init__(self, directory):
        self.cache = {}
        self.portal = Archive(directory / 'portal.dat')
        r = Reader(self.portal.read(0x34000000))
        assert r.word() == 0x34000000
        r.take(8)
        for _ in range(r.word()):
            r.word()
            r.string()
        self.types = {}
        for _ in range(r.word() & 65535):
            key = r.word()
            self.types[key] = r.word()
            r.take(16)

    def close(self):
        self.portal.file.close()
        self.cache.clear()

    def visual(self, did):
        if did not in self.cache:
            self.cache[did] = self.resolve_visual(did)
        return self.cache[did]

    def resolve_visual(self, did):
        seen = set()
        for _ in range(32):
            if did >> 24 == 0x1f:
                return did
            if did >> 24 != 0x47 or did in seen:
                return None
            seen.add(did)
            try:
                r = Reader(self.portal.read(did))
                desc = entity(r, self.types)
                if r.pos != len(r.data):
                    return None
            except (KeyError, ValueError, AssertionError):
                return None
            # EntityDesc::GetTotalProperties: an explicit PhysObj overrides the
            # parent. Otherwise follow the template chain, never a script class.
            did = desc.get('properties', {}).get('40000017:4100020B') or desc['data']
        return None


def landmark_position(cell, position):
    """Outdoor subcells share one global space; sealed rooms do not."""
    if 1 <= (cell & 65535) <= 64:
        return 0, (position[0] + (cell >> 24) * 160,
                   position[1] + ((cell >> 16) & 255) * 160, position[2])
    return cell, position


def nearby_landmark(cell, position, other_cell, other_position):
    space, point = landmark_position(cell, position)
    other_space, other_point = landmark_position(other_cell, other_position)
    return space == other_space and sum((a-b)**2 for a, b in zip(point, other_point)) <= 144
