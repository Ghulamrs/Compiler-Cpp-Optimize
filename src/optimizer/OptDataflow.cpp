#include "OptDataflow.h"

#include <algorithm>

namespace opt {

// A forward problem to a fixpoint: what reaches a block is the union of
// what leaves its predecessors, each register's last definition replacing
// the rest; an exception edge leaves at its call, the call's own made.
void ReachingDefs::solve(const Flow &f, const Stream &s, int registers,
                         const std::vector<std::vector<int>> &lastDef,
                         const std::vector<std::vector<int>> &unknownIn) {
    const int nb = static_cast<int>(f.blocks.size());
    in.assign(nb, std::vector<DefList>(registers));
    std::vector<std::vector<DefList>> out(nb, std::vector<DefList>(registers));
    std::vector<std::vector<DefList>> atEdge(f.edges.size(), std::vector<DefList>(registers));
    for (bool changed = true; changed;) {
        changed = false;
        for (int b = 0; b < nb; ++b) {
            for (int r = 0; r < registers; ++r) {
                DefList reach;
                if (unknownIn[b][r] >= 0) reach.push_back(unknownIn[b][r]);
                for (int e : f.blocks[b].preds) {
                    const DefList &from = f.edges[e].kind == Edge::Eh ? atEdge[e][r] : out[f.edges[e].from][r];
                    reach.insert(reach.end(), from.begin(), from.end());
                }
                std::sort(reach.begin(), reach.end());
                reach.erase(std::unique(reach.begin(), reach.end()), reach.end());
                in[b][r] = reach;
                for (int k = f.blocks[b].begin; k < f.blocks[b].end; ++k) {
                    if (s[k].kind != Entry::Ins || s[k].dead) continue;
                    const int d = lastDef[k][r];
                    if (d >= 0) reach.assign(1, d);
                    for (int e : f.blocks[b].succs)
                        if (f.edges[e].kind == Edge::Eh && f.edges[e].at == k && reach != atEdge[e][r]) {
                            atEdge[e][r] = reach;
                            changed = true;
                        }
                }
                if (reach != out[b][r]) { out[b][r] = reach; changed = true; }
            }
        }
    }
}

}
