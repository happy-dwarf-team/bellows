// range_witness decision logic against a fake game whose "original" walks like 0x3526B0: the
// country's own provinces, then those of qualifying related countries; the first eligible
// province below the threshold stops the walk; the written value is the running minimum
// truncated to thousands.
#include "../src/modules/range_witness_core.hpp"

#include <cassert>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

namespace {

struct Country {
    std::vector<std::int32_t> provinces;
    std::vector<Country*> related;  // nullptr = relation does not qualify
};

struct Target {
    std::int32_t id;
};

struct FakeGame {
    std::map<std::int32_t, bool> eligible;                                 // by province
    std::map<std::pair<std::int32_t, std::int32_t>, std::int64_t> values;  // (province, target) -> value
    int originals = 0;
    int evaluations = 0;
    std::int32_t day = 100;
    std::int32_t lieAbout = -1;  // Value() reports this province as 0 (a deliberately wrong formula)

    std::int64_t Walk(Country* c, std::int32_t target, const int* thr, std::int64_t best) {
        for (std::int32_t p : c->provinces) {
            if (!eligible[p]) continue;
            const std::int64_t v = values.at({p, target});
            if (v < best) best = v;
            if (thr != nullptr && best < *thr) return best;
        }
        return best;
    }
    int* Original(void* c, int* out, void* t, void*, int* thr) {
        ++originals;
        auto* country = static_cast<Country*>(c);
        const std::int32_t target = static_cast<Target*>(t)->id;
        std::int64_t best = Walk(country, target, thr, 0x5F5DD18);
        for (Country* r : country->related) {
            if (thr != nullptr && best < *thr) break;
            if (r != nullptr) best = Walk(r, target, thr, best);
        }
        *out = static_cast<int>(best) / 1000 * 1000;
        return out;
    }
    const std::int32_t* ListBegin(void* c) { return static_cast<Country*>(c)->provinces.data(); }
    const std::int32_t* ListEnd(void* c) {
        auto* country = static_cast<Country*>(c);
        return country->provinces.data() + country->provinces.size();
    }
    std::int32_t RelatedCount(void* c) { return static_cast<std::int32_t>(static_cast<Country*>(c)->related.size()); }
    void* Related(void* c, std::int32_t j) { return static_cast<Country*>(c)->related[j]; }
    bool Eligible(void*, std::int32_t p) { return eligible[p]; }
    bool Value(void*, bool, std::int32_t p, void* t, std::int64_t* v) {
        ++evaluations;
        *v = p == lieAbout ? 0 : values.at({p, static_cast<Target*>(t)->id});
        return true;
    }
    std::int32_t TargetId(void* t) { return static_cast<Target*>(t)->id; }
    std::int32_t Day() { return day; }
};

}  // namespace

int main() {
    Country russia{{1, 2, 3, 4, 5}, {}};
    Target siberia{900};
    FakeGame g;
    for (int p = 1; p <= 5; ++p) g.eligible[p] = true;
    g.values[{1, 900}] = 900'000;
    g.values[{2, 900}] = 800'000;
    g.values[{3, 900}] = 700'000;
    g.values[{4, 900}] = 150'500;
    g.values[{5, 900}] = 600'000;
    int threshold = 200'000;
    int out = 0;
    sb::WitnessEngine<FakeGame> e;

    // First check: original walk, stops at province 4; witness acquired.
    int* r = e.Check(g, &russia, &out, &siberia, nullptr, &threshold, false);
    assert(r == &out && out == 150'000 && g.originals == 1 && e.stats.acquired == 1 && e.size() == 1);

    // Second check: answered from the witness without the original, same value as the original's.
    g.evaluations = 0;
    r = e.Check(g, &russia, &out, &siberia, nullptr, &threshold, false);
    assert(r == &out && out == 150'000 && g.originals == 1 && e.stats.answered == 1 && g.evaluations == 1);

    // An earlier province now also qualifies: still in range (the original would stop earlier).
    g.values[{2, 900}] = 100'000;
    r = e.Check(g, &russia, &out, &siberia, nullptr, &threshold, false);
    assert(*r <= threshold && g.originals == 1 && e.stats.answered == 2);

    // The witness loses the walk's filter: the original runs and finds province 2.
    g.eligible[4] = false;
    r = e.Check(g, &russia, &out, &siberia, nullptr, &threshold, false);
    assert(out == 100'000 && g.originals == 2 && e.stats.witness_lost == 1 && e.stats.acquired == 2);
    r = e.Check(g, &russia, &out, &siberia, nullptr, &threshold, false);
    assert(g.originals == 2 && e.stats.answered == 3);

    // The witness leaves the country's list: original again; nothing else in range -> out of range.
    russia.provinces = {1, 3, 4, 5};
    r = e.Check(g, &russia, &out, &siberia, nullptr, &threshold, false);
    assert(*r > threshold && g.originals == 3 && e.stats.out_of_range == 1 && e.size() == 0);

    // In range only through a related country: the witness comes from its list.
    Country subject{{77, 78}, {}};
    g.eligible[77] = true;
    g.eligible[78] = true;
    g.values[{77, 900}] = 400'000;
    g.values[{78, 900}] = 120'000;
    russia.related = {nullptr, &subject};
    r = e.Check(g, &russia, &out, &siberia, nullptr, &threshold, false);
    assert(out == 120'000 && g.originals == 4 && e.stats.acquired == 3);
    r = e.Check(g, &russia, &out, &siberia, nullptr, &threshold, false);
    assert(out == 120'000 && g.originals == 4 && e.stats.answered_related == 1);
    // The relation stops qualifying: the witness is lost, the original says out of range.
    russia.related = {nullptr, nullptr};
    r = e.Check(g, &russia, &out, &siberia, nullptr, &threshold, false);
    assert(*r > threshold && e.stats.witness_lost == 3 && g.originals == 5);  // 1: filter, 2: left the list, 3: relation

    // In range but nothing below the threshold (value exactly at it): no witness; the lookup is
    // not repeated until kRetryDays later.
    g.values[{5, 900}] = 200'000;
    g.evaluations = 0;
    e.Check(g, &russia, &out, &siberia, nullptr, &threshold, false);
    assert(e.stats.not_acquired == 1 && g.evaluations > 0);
    g.evaluations = 0;
    e.Check(g, &russia, &out, &siberia, nullptr, &threshold, false);
    assert(e.stats.lookups_skipped == 1 && g.evaluations == 0);
    g.day += sb::WitnessEngine<FakeGame>::kRetryDays;
    e.Check(g, &russia, &out, &siberia, nullptr, &threshold, false);
    assert(e.stats.not_acquired == 2 && g.evaluations > 0);
    g.values[{5, 900}] = 600'000;

    // A different threshold is a different key; a missing threshold always runs the original.
    // (The pair at `threshold` still has its failed lookup on record: move past the retry day.)
    g.day += sb::WitnessEngine<FakeGame>::kRetryDays;
    g.eligible[4] = true;
    g.values[{4, 900}] = 150'500;
    int wider = 160'000;
    const int before = g.originals;
    e.Check(g, &russia, &out, &siberia, nullptr, &wider, false);
    e.Check(g, &russia, &out, &siberia, nullptr, &threshold, false);
    e.Check(g, &russia, &out, &siberia, nullptr, nullptr, false);
    assert(g.originals == before + 3);

    // Verify: the original always runs; answers agree; the original stopped at the witness itself.
    const int beforeVerify = g.originals;
    r = e.Check(g, &russia, &out, &siberia, nullptr, &threshold, true);
    assert(g.originals == beforeVerify + 1 && e.stats.verify_agree == 1 && e.stats.verify_value_same == 1 && out == 150'000);

    // Verify catches a wrong formula: the witness claims "in range" but the original says no.
    g.values[{4, 900}] = 500'000;
    g.lieAbout = 4;
    e.Check(g, &russia, &out, &siberia, nullptr, &threshold, true);
    assert(e.stats.verify_disagree == 1 && out > threshold);

    std::printf("ok: %lld checks, %lld answered, %lld originals\n", static_cast<long long>(e.stats.calls),
                static_cast<long long>(e.stats.answered), static_cast<long long>(e.stats.originals));
    return 0;
}
