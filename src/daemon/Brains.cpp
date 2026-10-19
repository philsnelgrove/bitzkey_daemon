#include "daemon/Brains.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "core/PyRandom.hpp"
#include "daemon/Wire.hpp"

namespace bitzkey::daemon {

namespace {

constexpr std::uint32_t BATCH_MAGIC = 0x314B5A42;   // "BZK1"
constexpr std::uint32_t ANSWER_MAGIC = 0x324B5A42;  // "BZK2"
constexpr int MEASURE_BRAINS = 128;
constexpr int MEASURE_ROUNDS = 3;
constexpr int MEASURE_DEPTH = 4;    // the depth of an animal of middling intellect
constexpr int MEASURE_REPLAYS = 4;  // memories it replays a turn

// A brain of this depth, its weights to be filled in.
Brain shaped(int depth) {
    Brain brain;
    brain.layers.resize(static_cast<std::size_t>(depth));
    for (int index = 0; index < depth; ++index) {
        Layer& layer = brain.layers[static_cast<std::size_t>(index)];
        layer.reads = index ? HIDDEN_SIZE : INPUT_SIZE;
        layer.weights.assign(static_cast<std::size_t>(HIDDEN_SIZE) * static_cast<std::size_t>(layer.reads), 0.0);
    }
    return brain;
}

void put_senses(wire::Writer& out, int ring, const FloatSenses& senses) {
    out.put<std::int8_t>(static_cast<std::int8_t>(ring));
    if (ring < 0) out.raw(senses.data(), sizeof(float) * INPUT_SIZE);
}

void get_senses(wire::Reader& in, int& ring, FloatSenses& senses) {
    ring = in.get<std::int8_t>();
    if (ring >= RING) throw std::runtime_error("senses from beyond the ring");
    if (ring < 0) in.raw(senses.data(), sizeof(float) * INPUT_SIZE);
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// The machine

Machine scan_machine() {
    Machine machine;
    machine.threads = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
#ifdef _WIN32
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof status;
    if (GlobalMemoryStatusEx(&status)) machine.memory = status.ullTotalPhys;
#else
    const long pages = sysconf(_SC_PHYS_PAGES), size = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && size > 0) machine.memory = static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(size);
#endif
    machine.gpus = gpu::scan();
    return machine;
}

std::uint64_t brain_bytes(int depth) {
    const std::uint64_t weights = static_cast<std::uint64_t>(HIDDEN_SIZE) * INPUT_SIZE +
                                  static_cast<std::uint64_t>(depth - 1) * HIDDEN_SIZE * HIDDEN_SIZE +
                                  static_cast<std::uint64_t>(depth) * HIDDEN_SIZE + ACTION_COUNT * HIDDEN_SIZE + ACTION_COUNT;
    return weights * 2 * sizeof(double) + RING * sizeof(FloatSenses) + 256;  // weights and traces, in doubles on a CPU
}

// ---------------------------------------------------------------------------------------------
// The wire

std::string encode(const Batch& batch) {
    wire::Writer out;
    out.put(BATCH_MAGIC);
    out.put(PROTOCOL);
    out.put(batch.session);
    out.put(static_cast<std::uint32_t>(batch.attaches.size()));
    for (const auto& [handle, brain] : batch.attaches) {
        out.put<std::int32_t>(handle);
        out.brain(brain);
    }
    out.put(static_cast<std::uint32_t>(batch.lessons.size()));
    for (const Batch::Lesson& lesson : batch.lessons) {
        out.put<std::int32_t>(lesson.handle);
        out.put<std::uint8_t>(static_cast<std::uint8_t>(lesson.action));
        out.put<std::uint8_t>(lesson.traced ? 1 : 0);
        out.put(lesson.target);
        out.put(lesson.rate);
        out.put(lesson.decay);
        put_senses(out, lesson.ring, lesson.senses);
    }
    out.put(static_cast<std::uint32_t>(batch.values.size()));
    for (const Batch::Ask& ask : batch.values) {
        out.put<std::int32_t>(ask.handle);
        put_senses(out, ask.ring, ask.senses);
    }
    out.put(static_cast<std::uint32_t>(batch.downloads.size()));
    for (const int handle : batch.downloads) out.put<std::int32_t>(handle);
    out.put(static_cast<std::uint32_t>(batch.detaches.size()));
    for (const int handle : batch.detaches) out.put<std::int32_t>(handle);
    return std::move(out.bytes());
}

Batch decode_batch(std::string_view bytes) {
    wire::Reader in(bytes);
    if (in.get<std::uint32_t>() != BATCH_MAGIC) throw std::runtime_error("not a batch of brains");
    if (in.get<std::uint32_t>() != PROTOCOL) throw std::runtime_error("a batch from another version of Bitzkey");
    Batch batch;
    batch.session = in.get<std::uint64_t>();
    const auto attaches = in.get<std::uint32_t>();
    for (std::uint32_t index = 0; index < attaches; ++index) {
        const int handle = in.get<std::int32_t>();
        batch.attaches.emplace_back(handle, in.brain());
    }
    const auto lessons = in.get<std::uint32_t>();
    batch.lessons.resize(lessons);
    for (Batch::Lesson& lesson : batch.lessons) {
        lesson.handle = in.get<std::int32_t>();
        lesson.action = in.get<std::uint8_t>();
        if (lesson.action >= ACTION_COUNT) throw std::runtime_error("a lesson in no action");
        lesson.traced = in.get<std::uint8_t>() != 0;
        lesson.target = in.get<float>();
        lesson.rate = in.get<float>();
        lesson.decay = in.get<float>();
        get_senses(in, lesson.ring, lesson.senses);
    }
    const auto values = in.get<std::uint32_t>();
    batch.values.resize(values);
    for (Batch::Ask& ask : batch.values) {
        ask.handle = in.get<std::int32_t>();
        get_senses(in, ask.ring, ask.senses);
    }
    const auto downloads = in.get<std::uint32_t>();
    for (std::uint32_t index = 0; index < downloads; ++index) batch.downloads.push_back(in.get<std::int32_t>());
    const auto detaches = in.get<std::uint32_t>();
    for (std::uint32_t index = 0; index < detaches; ++index) batch.detaches.push_back(in.get<std::int32_t>());
    if (!in.done()) throw std::runtime_error("a batch with more than it should have");
    return batch;
}

std::string encode(const Answer& answer) {
    wire::Writer out;
    out.put(ANSWER_MAGIC);
    out.put<std::uint8_t>(answer.known ? 1 : 0);
    out.put<std::uint8_t>(answer.leaving ? 1 : 0);
    out.put(answer.held);
    out.put(static_cast<std::uint32_t>(answer.values.size()));
    for (const auto& values : answer.values) out.raw(values.data(), sizeof(float) * ACTION_COUNT);
    out.put(static_cast<std::uint32_t>(answer.downloads.size()));
    for (const Brain& brain : answer.downloads) {
        out.put<std::uint8_t>(brain.layers.empty() ? 0 : 1);  // none: the worker didn't hold it
        if (!brain.layers.empty()) out.brain(brain);
    }
    return std::move(out.bytes());
}

Answer decode_answer(std::string_view bytes) {
    wire::Reader in(bytes);
    if (in.get<std::uint32_t>() != ANSWER_MAGIC) throw std::runtime_error("not an answer from a worker");
    Answer answer;
    answer.known = in.get<std::uint8_t>() != 0;
    answer.leaving = in.get<std::uint8_t>() != 0;
    answer.held = in.get<std::uint32_t>();
    answer.values.resize(in.get<std::uint32_t>());
    for (auto& values : answer.values) in.raw(values.data(), sizeof(float) * ACTION_COUNT);
    const auto downloads = in.get<std::uint32_t>();
    for (std::uint32_t index = 0; index < downloads; ++index) answer.downloads.push_back(in.get<std::uint8_t>() ? in.brain() : Brain{});
    return answer;
}

// ---------------------------------------------------------------------------------------------
// Brains on the CPU

void parallel_for(std::size_t count, int threads, const std::function<void(std::size_t)>& work) {
    const std::size_t helpers = std::min<std::size_t>(count, static_cast<std::size_t>(std::max(1, threads))) ;
    if (helpers <= 1) {
        for (std::size_t index = 0; index < count; ++index) work(index);
        return;
    }
    std::atomic<std::size_t> next{0};
    const auto run = [&] {
        for (std::size_t index; (index = next.fetch_add(1)) < count;) work(index);
    };
    std::vector<std::thread> started;
    started.reserve(helpers - 1);
    for (std::size_t index = 1; index < helpers; ++index) started.emplace_back(run);
    run();
    for (std::thread& thread : started) thread.join();
}

CpuEngine::CpuEngine(int threads) : threads_(std::max(1, threads)) {}

CpuEngine::Handle CpuEngine::attach(const Brain& brain) {
    Handle handle;
    if (!free_.empty()) {
        handle = free_.back();
        free_.pop_back();
    } else {
        handle = static_cast<Handle>(brains_.size());
        brains_.emplace_back();
    }
    brains_[static_cast<std::size_t>(handle)] = brain;
    ++attached_;
    return handle;
}

void CpuEngine::detach(Handle handle) {
    auto& brain = brains_.at(static_cast<std::size_t>(handle));
    if (!brain) return;
    brain.reset();
    free_.push_back(handle);
    --attached_;
}

void CpuEngine::download(std::span<const Handle> brains, std::span<Brain* const> into) {
    for (std::size_t index = 0; index < brains.size(); ++index) {
        const Brain& held = *brains_.at(static_cast<std::size_t>(brains[index]));
        Brain& brain = *into[index];
        brain.layers = held.layers;
        brain.output_weights = held.output_weights;
        brain.output_biases = held.output_biases;
    }
}

void CpuEngine::values(std::span<const Handle> brains, std::span<const Senses> senses, std::span<Values> out) {
    parallel_for(brains.size(), threads_, [&](std::size_t index) {
        out[index] = brains_.at(static_cast<std::size_t>(brains[index]))->values(senses[index]);
    });
}

void CpuEngine::learn(std::span<const Lesson> lessons) {
    // Each brain's lessons in the order given, the brains side by side.
    std::unordered_map<Handle, std::vector<std::size_t>> by_brain;
    std::vector<Handle> order;
    for (std::size_t index = 0; index < lessons.size(); ++index) {
        auto [found, fresh] = by_brain.try_emplace(lessons[index].brain);
        if (fresh) order.push_back(lessons[index].brain);
        found->second.push_back(index);
    }
    parallel_for(order.size(), threads_, [&](std::size_t place) {
        Brain& brain = *brains_.at(static_cast<std::size_t>(order[place]));
        for (const std::size_t index : by_brain.at(order[place])) {
            const Lesson& lesson = lessons[index];
            if (lesson.traced) brain.learn_traced_by(*lesson.senses, lesson.action, lesson.target, lesson.rate, lesson.decay);
            else brain.learn(*lesson.senses, lesson.action, lesson.target, lesson.rate);
        }
    });
}

// ---------------------------------------------------------------------------------------------
// The brains a worker holds

Thinker::Thinker(const Resources& resources) : resources_(resources) {
    engine_ = make_engine(resources_, on_gpu_);
    measure();
}

std::unique_ptr<gpu::BrainEngine> Thinker::make_engine(const Resources& resources, bool& gpu) const {
    if (!resources.gpus.empty()) {
        if (auto engine = gpu::BrainEngine::create_on(resources.gpus)) {
            gpu = true;
            return engine;
        }
    }
    gpu = false;
    return std::make_unique<CpuEngine>(resources.threads);
}

void Thinker::share(const Resources& resources) {
    const bool moved = resources.gpus != resources_.gpus;
    resources_ = resources;
    if (!moved) {
        if (auto* cpu = dynamic_cast<CpuEngine*>(engine_.get())) cpu->set_threads(resources_.threads);
        measure();
        return;
    }
    // Every brain held brought back, and put on the GPUs now chosen, or the CPU: its traces start again there.
    bool gpu = false;
    auto engine = make_engine(resources_, gpu);
    for (auto& [handle, held] : held_) {
        Brain brain = shaped(held.depth);
        Brain* into[] = {&brain};
        const gpu::BrainEngine::Handle handles[] = {held.inner};
        engine_->download(handles, into);
        held.inner = engine->attach(brain);
    }
    engine_ = std::move(engine);
    on_gpu_ = gpu;
    measure();
}

const FloatSenses& Thinker::senses_of(Held& held, int ring, const FloatSenses& given) {
    if (ring >= 0) return held.ring[static_cast<std::size_t>(ring)];
    FloatSenses& kept = held.ring[static_cast<std::size_t>(held.next)];
    kept = given;
    held.next = (held.next + 1) % RING;
    return kept;
}

Answer Thinker::apply(const Batch& batch) {
    Answer answer;
    for (const auto& [handle, brain] : batch.attaches) {
        if (const auto found = held_.find(handle); found != held_.end()) {
            engine_->detach(found->second.inner);
            held_.erase(found);
        }
        held_.emplace(handle, Held{engine_->attach(brain), brain.depth()});
    }
    if (!batch.lessons.empty()) {
        std::vector<Senses> senses(batch.lessons.size());
        std::vector<gpu::BrainEngine::Lesson> lessons;
        lessons.reserve(batch.lessons.size());
        for (std::size_t index = 0; index < batch.lessons.size(); ++index) {
            const Batch::Lesson& lesson = batch.lessons[index];
            const auto found = held_.find(lesson.handle);
            if (found == held_.end()) continue;  // a brain let go since
            const FloatSenses& felt = senses_of(found->second, lesson.ring, lesson.senses);
            std::copy(felt.begin(), felt.end(), senses[index].begin());
            lessons.push_back({found->second.inner, &senses[index], lesson.action, lesson.target, lesson.rate, lesson.traced, lesson.decay});
        }
        engine_->learn(lessons);
    }
    if (!batch.values.empty()) {
        std::vector<gpu::BrainEngine::Handle> handles;
        std::vector<Senses> senses;
        std::vector<std::size_t> places;
        for (std::size_t index = 0; index < batch.values.size(); ++index) {
            const Batch::Ask& ask = batch.values[index];
            const auto found = held_.find(ask.handle);
            if (found == held_.end()) continue;
            const FloatSenses& felt = senses_of(found->second, ask.ring, ask.senses);
            Senses& into = senses.emplace_back();
            std::copy(felt.begin(), felt.end(), into.begin());
            handles.push_back(found->second.inner);
            places.push_back(index);
        }
        std::vector<Values> values(handles.size());
        engine_->values(handles, senses, values);
        answer.values.assign(batch.values.size(), {});
        for (auto& missing : answer.values) missing.fill(std::nanf(""));  // a brain it doesn't hold
        for (std::size_t k = 0; k < places.size(); ++k) {
            for (int action = 0; action < ACTION_COUNT; ++action) answer.values[places[k]][action] = static_cast<float>(values[k][action]);
        }
    }
    if (!batch.downloads.empty()) {
        answer.downloads.resize(batch.downloads.size());
        std::vector<gpu::BrainEngine::Handle> handles;
        std::vector<Brain*> into;
        for (std::size_t index = 0; index < batch.downloads.size(); ++index) {
            const auto found = held_.find(batch.downloads[index]);
            if (found == held_.end()) continue;
            answer.downloads[index] = shaped(found->second.depth);
            handles.push_back(found->second.inner);
            into.push_back(&answer.downloads[index]);
        }
        engine_->download(handles, into);
    }
    for (const int handle : batch.detaches) {
        if (const auto found = held_.find(handle); found != held_.end()) {
            engine_->detach(found->second.inner);
            held_.erase(found);
        }
    }
    answer.held = static_cast<std::uint32_t>(held_.size());
    return answer;
}

void Thinker::clear() {
    for (const auto& [handle, held] : held_) engine_->detach(held.inner);
    held_.clear();
}

std::string Thinker::description() const {
    if (on_gpu_) {
        std::string names;
        for (const gpu::DeviceInfo& device : engine_->devices()) names += (names.empty() ? "" : "; ") + device.name;
        return names;
    }
    return std::to_string(resources_.threads) + (resources_.threads == 1 ? " CPU thread" : " CPU threads");
}

void Thinker::measure() {
    PyRandom random(20261019);
    std::vector<gpu::BrainEngine::Handle> handles;
    std::vector<Senses> senses(MEASURE_BRAINS);
    for (int index = 0; index < MEASURE_BRAINS; ++index) {
        handles.push_back(engine_->attach(Brain::random(random, MEASURE_DEPTH)));
        for (double& sense : senses[static_cast<std::size_t>(index)]) sense = random.random();
    }
    std::vector<Values> values(handles.size());
    std::vector<gpu::BrainEngine::Lesson> lessons;
    for (std::size_t index = 0; index < handles.size(); ++index) {
        lessons.push_back({handles[index], &senses[index], 0, 0.5, 0.01, true, 0.8});
        for (int replay = 0; replay < MEASURE_REPLAYS; ++replay) lessons.push_back({handles[index], &senses[index], replay + 1, 0.5, 0.01, false, 0.0});
    }
    try {
        engine_->values(handles, senses, values);  // once to warm up: the brains go up to the GPUs
        const auto start = std::chrono::steady_clock::now();
        for (int round = 0; round < MEASURE_ROUNDS; ++round) {
            engine_->values(handles, senses, values);
            engine_->learn(lessons);
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        speed_ = MEASURE_BRAINS * MEASURE_ROUNDS / std::max(seconds, 1e-6);
    } catch (const std::runtime_error&) {
        speed_ = 0.0;
    }
    for (const auto handle : handles) engine_->detach(handle);
}

}  // namespace bitzkey::daemon
