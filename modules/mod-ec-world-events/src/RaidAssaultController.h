/**
 * @file RaidAssaultController.h
 * @brief Deterministic, engine-independent, staged assault policy.
 * @date 2026-09-26
 *
 * Custom design source: owner's staged open-world invasion requirements in
 * this conversation. This is NOT recovered Blizzard/Alterac Valley behavior.
 * Engine integration contract: only verified navigation corridors may satisfy
 * ReportPath(Success). Unknown defenders are not dead; time never clears a post.
 */
#ifndef EC_RAID_ASSAULT_CONTROLLER_H
#define EC_RAID_ASSAULT_CONTROLLER_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

namespace EC::RaidAssault
{
using Id = std::uint64_t;
using Time = std::uint64_t;

struct Point
{
    double x = 0;
    double y = 0;
    double z = 0;
    [[nodiscard]] bool IsFinite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
};

inline double DistanceSquared(Point const& a, Point const& b)
{
    return (a.x-b.x)*(a.x-b.x) + (a.y-b.y)*(a.y-b.y) + (a.z-b.z)*(a.z-b.z);
}

enum class Tier : std::uint8_t { Outer, Military, Inner };
enum class Source : std::uint8_t { ResidentSpawn, AssignedBuilding };

struct Candidate
{
    Id id = 0;
    Point position;
    Tier tier = Tier::Outer;
    Source source = Source::ResidentSpawn;
};

struct Objective
{
    Id id = 0;
    Point position;
    Tier tier = Tier::Outer;
    std::uint32_t approach = 0;
    std::vector<Id> sources;
    std::vector<Id> prerequisites;
};

inline std::vector<Objective> BuildObjectives(std::vector<Candidate> candidates,
    Point center, double clusterRadius = 18, std::uint32_t approaches = 4)
{
    if (!center.IsFinite() || !std::isfinite(clusterRadius) || clusterRadius <= 0 ||
        approaches == 0 || approaches > 32 || candidates.size() > 4096)
        throw std::invalid_argument("Invalid objective geometry or candidate limit");
    std::sort(candidates.begin(), candidates.end(), [](auto const& a, auto const& b) { return a.id < b.id; });
    std::vector<Objective> plan;
    Id previous = 0;
    constexpr double pi = 3.14159265358979323846;
    for (Candidate const& c : candidates)
    {
        if (!c.id || c.id == previous || !c.position.IsFinite() || static_cast<unsigned>(c.tier) > 2)
            throw std::invalid_argument("Invalid or duplicate objective source");
        previous = c.id;
        double angle = std::atan2(c.position.y-center.y, c.position.x-center.x) + pi;
        auto lane = std::min(approaches-1, static_cast<std::uint32_t>(angle/(2*pi)*approaches));
        auto found = std::find_if(plan.begin(), plan.end(), [&](Objective const& o)
        {
            return o.approach == lane && o.tier == c.tier &&
                DistanceSquared(o.position, c.position) <= clusterRadius*clusterRadius;
        });
        if (found == plan.end())
            plan.push_back({c.id, c.position, c.tier, lane, {c.id}, {}});
        else
            found->sources.push_back(c.id);
    }
    for (Objective& o : plan)
        for (Objective const& earlier : plan)
            if (earlier.approach == o.approach && earlier.tier < o.tier)
                o.prerequisites.push_back(earlier.id);
    return plan;
}

struct Config
{
    Time firstWaveDelay = 15000;
    Time waveInterval = 20000;
    std::uint32_t waveSize = 4;
    Time holdTime = 10000;
    Time retryDelay = 5000;
    Time stuckTimeout = 20000;
    std::uint32_t maxPathFailures = 3;
    double arrivalRadius = 8;
    double progressDistance = 1;
};

struct Actor
{
    Id id = 0;
    Point position;
    bool eligible = false;
    bool alive = true;
    bool inCombat = false;
    bool movementPrevented = false;
    bool moving = false;
};

struct Observation
{
    bool known = false;
    std::uint32_t livingDefenders = 0;
    bool contested = false;
};

enum class CommandKind { Hold, Move, Engage, ObjectiveCleared, Blocked, Stop };
struct Command
{
    CommandKind kind;
    Id actor = 0;
    Id objective = 0;
    Point destination;
};
enum class PathResult { Success, Unreachable };

struct ActorState
{
    bool released = false;
    bool blocked = false;
    bool awaitingPath = false;
    bool moving = false;
    bool suspended = false;
    std::optional<Id> target;
    Time nextAction = 0;
    Time lastProgress = 0;
    std::uint32_t failures = 0;
    Point progressPosition;
};

class Controller
{
public:
    explicit Controller(Config config = {}) : _config(config)
    {
        if (!_config.waveSize || !_config.waveInterval || !_config.maxPathFailures ||
            !_config.stuckTimeout || !_config.retryDelay || !_config.holdTime ||
            !std::isfinite(_config.arrivalRadius) || _config.arrivalRadius <= 0 ||
            !std::isfinite(_config.progressDistance) || _config.progressDistance <= 0)
            throw std::invalid_argument("Invalid assault policy");
    }

    void Start(std::vector<Objective> plan, Time now)
    {
        if (plan.empty())
            throw std::invalid_argument("No data-backed assault objectives");
        std::map<Id, Objective> objectives;
        for (auto& o : plan)
        {
            if (!o.id || !o.position.IsFinite() || static_cast<unsigned>(o.tier) > 2 ||
                !objectives.emplace(o.id, o).second)
                throw std::invalid_argument("Invalid or duplicate objective");
        }
        for (auto const& [id, o] : objectives)
        {
            (void)id;
            for (Id required : o.prerequisites)
                if (!objectives.count(required) || objectives.at(required).tier >= o.tier)
                    throw std::invalid_argument("Missing, cyclic, or non-forward prerequisite");
        }
        _objectives = std::move(objectives);
        _actors.clear();
        _cleared.clear();
        _holds.clear();
        _nextWave = Add(now, _config.firstWaveDelay);
        _lastTime = now;
        _active = true;
    }

    [[nodiscard]] std::map<Id, ActorState> const& Actors() const { return _actors; }
    [[nodiscard]] std::map<Id, Objective> const& Objectives() const { return _objectives; }
    [[nodiscard]] bool IsCleared(Id id) const { return _cleared.count(id) != 0; }
    [[nodiscard]] bool IsActive() const { return _active; }

    void DeferPath(Id actor, Time now)
    {
        auto it = _actors.find(actor);
        if (it == _actors.end() || !it->second.awaitingPath)
            return;
        it->second.awaitingPath = false;
        it->second.nextAction = Add(now, 1);
    }

    void ReportPath(Id actor, PathResult result, Time now)
    {
        auto it = _actors.find(actor);
        if (!_active || it == _actors.end() || !it->second.awaitingPath)
            return;
        auto& s = it->second;
        s.awaitingPath = false;
        s.moving = result == PathResult::Success;
        if (s.moving)
        {
            if (s.lastProgress == 0)
                s.lastProgress = now;
        }
        else
        {
            ++s.failures;
            s.blocked = s.failures >= _config.maxPathFailures;
            s.nextAction = Add(now, _config.retryDelay);
        }
    }

    std::vector<Command> Update(Time now, bool eventActive, std::vector<Actor> actors,
        std::map<Id, Observation> const& observations)
    {
        std::vector<Command> out;
        if (!_active)
            return out;
        now = std::max(now, _lastTime);
        _lastTime = now;
        std::sort(actors.begin(), actors.end(), [](auto const& a, auto const& b) { return a.id < b.id; });
        std::set<Id> observed;
        for (auto const& a : actors)
            if (!a.id || !observed.insert(a.id).second)
                throw std::invalid_argument("Invalid or duplicate actor");
        if (!eventActive)
        {
            for (auto const& a : actors)
                if (a.eligible && _actors.count(a.id))
                    out.push_back({CommandKind::Stop, a.id, 0, {}});
            _active = false;
            _actors.clear();
            _holds.clear();
            return out;
        }

        std::uint32_t released = 0;
        for (Actor const& a : actors)
        {
            if (!Ready(a))
                continue;
            ActorState& s = _actors[a.id];
            if (!s.released && now >= _nextWave && released < _config.waveSize && !a.inCombat)
            {
                s.released = true;
                s.progressPosition = a.position;
                s.lastProgress = now;
                ++released;
            }
            if (!s.released && !a.inCombat)
                out.push_back({CommandKind::Hold, a.id, 0, {}});
        }
        if (released)
            _nextWave = Add(now, _config.waveInterval);

        for (auto const& [id, o] : _objectives)
        {
            if (IsCleared(id) || !Unlocked(o))
                continue;
            auto observation = observations.find(id);
            bool secure = observation != observations.end() && observation->second.known &&
                observation->second.livingDefenders == 0 && !observation->second.contested;
            bool occupied = false;
            for (Actor const& a : actors)
            {
                auto state = _actors.find(a.id);
                if (!Ready(a) || state == _actors.end() || !state->second.released ||
                    DistanceSquared(a.position, o.position) > _config.arrivalRadius*_config.arrivalRadius)
                    continue;
                if (a.inCombat)
                    secure = false;
                else
                    occupied = true;
            }
            if (!secure || !occupied)
            {
                _holds.erase(id);
                continue;
            }
            auto [hold, inserted] = _holds.emplace(id, now);
            if (!inserted && now-hold->second >= _config.holdTime)
            {
                _cleared.insert(id);
                out.push_back({CommandKind::ObjectiveCleared, 0, id, o.position});
            }
        }

        for (Actor const& a : actors)
        {
            auto it = _actors.find(a.id);
            if (it == _actors.end() || !it->second.released)
                continue;
            ActorState& s = it->second;
            if (!Ready(a) || a.inCombat || a.movementPrevented)
            {
                s.suspended = true;
                s.moving = false;
                s.awaitingPath = false;
                continue;
            }
            if (s.suspended)
            {
                s.suspended = false;
                s.lastProgress = now;
                s.progressPosition = a.position;
            }
            if (s.target && IsCleared(*s.target))
            {
                s = ActorState{};
                s.released = true;
            }
            if (!s.target)
                s.target = Choose(a.position, actors);
            if (!s.target)
                continue;
            Objective const& target = _objectives.at(*s.target);
            if (s.blocked)
                continue;
            if (DistanceSquared(a.position, target.position) <= _config.arrivalRadius*_config.arrivalRadius)
            {
                s.moving = false;
                s.awaitingPath = false;
                s.failures = 0;
                auto observation = observations.find(*s.target);
                if (now >= s.nextAction && observation != observations.end() && observation->second.known &&
                    (observation->second.livingDefenders || observation->second.contested))
                {
                    out.push_back({CommandKind::Engage, a.id, *s.target, target.position});
                    s.nextAction = Add(now, 1500);
                }
                continue;
            }
            if (s.moving)
            {
                if (DistanceSquared(a.position, s.progressPosition) >= _config.progressDistance*_config.progressDistance)
                {
                    s.lastProgress = now;
                    s.progressPosition = a.position;
                    s.failures = 0;
                }
                if (now-s.lastProgress >= _config.stuckTimeout)
                {
                    s.moving = false;
                    ++s.failures;
                    s.lastProgress = now;
                    s.nextAction = Add(now, _config.retryDelay);
                    s.blocked = s.failures >= _config.maxPathFailures;
                    if (s.blocked)
                        out.push_back({CommandKind::Blocked, a.id, *s.target, target.position});
                }
                else if (a.moving)
                    continue;
                else
                {
                    s.moving = false;
                    s.nextAction = Add(now, _config.retryDelay);
                }
            }
            if (!s.blocked && !s.awaitingPath && now >= s.nextAction)
            {
                s.awaitingPath = true;
                s.progressPosition = a.position;
                out.push_back({CommandKind::Move, a.id, *s.target, target.position});
            }
        }
        return out;
    }

private:
    [[nodiscard]] bool Ready(Actor const& a) const { return a.eligible && a.alive && a.position.IsFinite(); }
    static Time Add(Time a, Time b) { return b > std::numeric_limits<Time>::max()-a ? std::numeric_limits<Time>::max() : a+b; }
    [[nodiscard]] bool Unlocked(Objective const& o) const
    {
        return std::all_of(o.prerequisites.begin(), o.prerequisites.end(), [&](Id id) { return IsCleared(id); });
    }
    std::optional<Id> Choose(Point point, std::vector<Actor> const& actors) const
    {
        std::optional<Id> best;
        std::size_t load = std::numeric_limits<std::size_t>::max();
        double distance = std::numeric_limits<double>::infinity();
        for (auto const& [id, o] : _objectives)
        {
            if (IsCleared(id) || !Unlocked(o))
                continue;
            std::size_t assigned = 0;
            for (auto const& a : actors)
            {
                auto state = _actors.find(a.id);
                if (Ready(a) && state != _actors.end() && !state->second.blocked && state->second.target == id)
                    ++assigned;
            }
            double d = DistanceSquared(point, o.position);
            if (assigned < load || (assigned == load && d < distance))
            {
                best = id;
                load = assigned;
                distance = d;
            }
        }
        return best;
    }
    Config _config;
    std::map<Id, Objective> _objectives;
    std::map<Id, ActorState> _actors;
    std::set<Id> _cleared;
    std::map<Id, Time> _holds;
    Time _nextWave = 0;
    Time _lastTime = 0;
    bool _active = false;
};
}
#endif
