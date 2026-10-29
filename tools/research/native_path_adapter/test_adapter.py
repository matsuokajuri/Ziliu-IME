import copy
import unittest
import adapter as a


def fixture():
    graph = {'total': 3, 'entries': [{'text': t, 'weight': w} for t, w in [('A', 14.), ('B', 15.), ('C', 16.)]],
             'edges': [[0, 1, 0], [1, 3, 1], [1, 3, 2]]}
    residuals = [.25, .5, -.75]
    b = [w['weight'] + a.PENALTY for w in graph['entries']]
    nodes = [[2, 1, 0, 1, 0, b[0] + .25, b[0]],
             [3, 2, 1, 3, 1, (b[0] + .25) + (b[1] + .5), b[0] + b[1]],
             [4, 2, 1, 3, 2, (b[0] + .25) + (b[2] - .75), b[0] + b[2]]]
    pool = [dict(node=n, text=t, start=0, end=3, weight=nodes[n-2][6],
                 retained_weight=nodes[n-2][6], word_lengths=[1, 2]) for n, t in [(3, 'AB'), (4, 'AC')]]
    snap = dict(nodes=nodes, attempts=3, pool=pool, pool_ids=[3, 4], states=[[0, [1]], [1, [2]], [2, []], [3, [3, 4]]], best=[3, 4])
    return graph, residuals, snap


class Tests(unittest.TestCase):
    def reject(self, change):
        g, r, s = fixture()
        change(g, r, s)
        with self.assertRaises((ValueError, TypeError, KeyError)):
            a.adapt_candidate_paths(g, r, s)

    def test_shared_prefix_and_empty_state(self):
        paths, meta = a.adapt_candidate_paths(*fixture())
        self.assertEqual(paths, [(0, 1), (0, 2)])
        self.assertFalse(meta['native_integration_admitted'])

    def test_unordered_nodes(self):
        g, r, s = fixture()
        s['nodes'].reverse()
        self.assertEqual(a.adapt_candidate_paths(g, r, s)[0], [(0, 1), (0, 2)])

    def test_missing_parent(self):
        self.reject(lambda g, r, s: s['nodes'].pop(0))

    def test_cycle(self):
        self.reject(lambda g, r, s: s['nodes'][0].__setitem__(1, 3))

    def test_duplicate_id(self):
        self.reject(lambda g, r, s: s['nodes'][1].__setitem__(0, 2))

    def test_wrong_text(self):
        self.reject(lambda g, r, s: s['pool'][0].__setitem__('text', 'WRONG'))

    def test_wrong_end(self):
        self.reject(lambda g, r, s: s['pool'][0].__setitem__('end', 2))

    def test_wrong_score(self):
        self.reject(lambda g, r, s: s['nodes'][1].__setitem__(5, 99.))

    def test_search_not_base(self):
        self.reject(lambda g, r, s: s['pool'][0].__setitem__('weight', s['nodes'][1][5]))
        self.reject(lambda g, r, s: s['nodes'][1].__setitem__(5, s['nodes'][1][6]))

    def test_wrong_order(self):
        self.reject(lambda g, r, s: s['pool_ids'].reverse())

    def test_nonfinite_and_bool(self):
        self.reject(lambda g, r, s: r.__setitem__(0, float('nan')))
        self.reject(lambda g, r, s: s['nodes'][0].__setitem__(6, float('inf')))
        self.reject(lambda g, r, s: s['nodes'][0].__setitem__(1, True))

    def test_wrong_edge_or_disconnection(self):
        self.reject(lambda g, r, s: s['nodes'][0].__setitem__(4, 99))
        self.reject(lambda g, r, s: s['nodes'][1].__setitem__(1, 1))

    def test_fullspan(self):
        def change(g, r, s):
            g['edges'][0][1] = 3
            s['nodes'][0][3] = 3
        self.reject(change)

    def test_bad_state_or_best(self):
        self.reject(lambda g, r, s: s['states'].append([2, [3]]))
        self.reject(lambda g, r, s: s['best'].append(2))

    def test_tolerance(self):
        g, r, s = fixture()
        s['nodes'][1][5] += 1e-12
        a.adapt_candidate_paths(g, r, s)
        self.reject(lambda g, r, s: s['nodes'][1].__setitem__(5, s['nodes'][1][5] + 1e-5))

    def test_segmentation(self):
        self.reject(lambda g, r, s: s['pool'][0].__setitem__('word_lengths', [2, 1]))


if __name__ == '__main__':
    unittest.main()
