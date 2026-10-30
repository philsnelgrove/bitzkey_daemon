#include "core/GpuBrains.hpp"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <algorithm>
#include <execution>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <unordered_map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "core/shaders/learn.spv.h"
#include "core/shaders/values.spv.h"

namespace bitzkey::gpu {

namespace {

std::string environment(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || !value) return {};
    std::string found(value);
    std::free(value);
    return found;
#else
    const char* value = std::getenv(name);
    return value ? value : "";
#endif
}

// ---------------------------------------------------------------------------------------------
// Vulkan, loaded at run time, so the program runs on machines without it.

#define BITZKEY_VULKAN_FUNCTIONS(X)                                                                         \
    X(vkCreateInstance) X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceMemoryProperties) X(vkCreateDevice)    \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkCreateBuffer) X(vkDestroyBuffer)                             \
    X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) \
    X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateDescriptorSetLayout)                         \
    X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout)                    \
    X(vkCreateComputePipelines) X(vkDestroyPipeline) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool)   \
    X(vkResetDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) X(vkCreateCommandPool)    \
    X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer)       \
    X(vkResetCommandBuffer) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdPushConstants)          \
    X(vkCmdDispatch) X(vkCmdCopyBuffer) X(vkCmdPipelineBarrier) X(vkCreateFence) X(vkDestroyFence)          \
    X(vkResetFences) X(vkWaitForFences) X(vkQueueSubmit) X(vkDeviceWaitIdle)

struct Vulkan {
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
#define BITZKEY_DECLARE(name) PFN_##name name = nullptr;
    BITZKEY_VULKAN_FUNCTIONS(BITZKEY_DECLARE)
#undef BITZKEY_DECLARE
    void* library = nullptr;

    bool load_library() {
#ifdef _WIN32
        library = reinterpret_cast<void*>(LoadLibraryA("vulkan-1.dll"));
        if (!library) return false;
        vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), "vkGetInstanceProcAddr")));
#else
        library = dlopen("libvulkan.so.1", RTLD_NOW);
        if (!library) return false;
        vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library, "vkGetInstanceProcAddr"));
#endif
        if (!vkGetInstanceProcAddr) return false;
        vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(vkGetInstanceProcAddr(nullptr, "vkCreateInstance"));
        return vkCreateInstance != nullptr;
    }

    void load_instance(VkInstance instance) {
#define BITZKEY_LOAD(name) name = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr(instance, #name));
        BITZKEY_VULKAN_FUNCTIONS(BITZKEY_LOAD)
#undef BITZKEY_LOAD
    }

    ~Vulkan() {
#ifdef _WIN32
        if (library) FreeLibrary(static_cast<HMODULE>(library));
#else
        if (library) dlclose(library);
#endif
    }
};

void check(VkResult result, const char* what) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string("Vulkan: ") + what + " failed (" + std::to_string(result) + ")");
}

// ---------------------------------------------------------------------------------------------
// A brain as the shaders lay it out: see shaders/brain.glsl.

using Shape = std::pair<int, int>;  // a brain's depth and width: its pool

std::uint32_t slot_floats(Shape shape) {
    const auto [depth, width] = shape;
    return static_cast<std::uint32_t>(width * INPUT_SIZE + (depth - 1) * width * width + depth * width + OUTPUT_COUNT * width +
                                      OUTPUT_COUNT);
}

Shape shape_of(const Brain& brain) { return {brain.depth(), brain.width()}; }
static_assert(MAX_DEPTH == 4 && MAX_WIDTH == 53, "shaders/brain.glsl's MAX_DEPTH and MAX_HIDDEN must match");
static_assert(INPUT_SIZE == 153 && OUTPUT_COUNT == 48 && SenseMemory == 74 && MEMORY_GATES == 32 && MEMORY_NEURONS == 8,
              "shaders/brain.glsl's INPUTS, ACTIONS, MEMORY_SENSE, MEMORY_GATES and MEMORY_NEURONS must match");

// The layers, input by input, then the biases, then the outputs, neuron by neuron, then their biases:
// for a brain's weights, or its traces.
void pack_parts(const std::vector<Layer>& layers, const Brain::OutputWeights& outputs, const Brain::OutputBiases& biases,
                float* at) {
    for (const Layer& layer : layers) {
        for (int item = 0; item < layer.reads; ++item) {
            for (int neuron = 0; neuron < layer.neurons(); ++neuron) *at++ = static_cast<float>(layer.row(neuron)[item]);
        }
    }
    for (const Layer& layer : layers) {
        for (const double bias : layer.biases) *at++ = static_cast<float>(bias);
    }
    for (std::size_t neuron = 0; neuron < outputs[0].size(); ++neuron) {
        for (int output = 0; output < OUTPUT_COUNT; ++output) *at++ = static_cast<float>(outputs[output][neuron]);
    }
    for (const double bias : biases) *at++ = static_cast<float>(bias);
}

void unpack_parts(const float* at, std::vector<Layer>& layers, Brain::OutputWeights& outputs, Brain::OutputBiases& biases) {
    for (Layer& layer : layers) {
        for (int item = 0; item < layer.reads; ++item) {
            for (int neuron = 0; neuron < layer.neurons(); ++neuron) layer.row(neuron)[item] = *at++;
        }
    }
    for (Layer& layer : layers) {
        for (double& bias : layer.biases) bias = *at++;
    }
    for (std::size_t neuron = 0; neuron < outputs[0].size(); ++neuron) {
        for (int output = 0; output < OUTPUT_COUNT; ++output) outputs[output][neuron] = *at++;
    }
    for (double& bias : biases) bias = *at++;
}

void pack(const Brain& brain, float* weights, float* traces) {
    pack_parts(brain.layers, brain.output_weights, brain.output_biases, weights);
    if (brain.traces) pack_parts(brain.traces->layers, brain.traces->output_weights, brain.traces->output_biases, traces);
    else std::fill(traces, traces + slot_floats(shape_of(brain)), 0.0f);
}


struct Push {
    std::uint32_t depth, count, slot_floats, first, width;
};

struct GpuGroup {
    std::uint32_t slot, first, count, unused;
};

struct GpuLesson {
    std::uint32_t action, traced;      // 0 plain, 1 traced, 2 back in time, 3 the senses of a choice before
    float target, rate, decay, limit;  // the lesson limit
    std::uint32_t steps, unused;
};
static_assert(sizeof(GpuLesson) == 32);

// ---------------------------------------------------------------------------------------------

class VulkanEngine final : public BrainEngine {
public:
    // On the discrete GPUs, else the integrated; with `all_kinds`, both; or, given `only`, those of scan()'s list.
    explicit VulkanEngine(bool all_kinds, const std::vector<int>* only = nullptr) {
        start(vk_, instance_);
        std::vector<VkPhysicalDevice> discrete, integrated, both;
        for (VkPhysicalDevice device : list(vk_, instance_)) {
            VkPhysicalDeviceProperties properties;
            vk_.vkGetPhysicalDeviceProperties(device, &properties);
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) discrete.push_back(device);
            else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) integrated.push_back(device);
            else continue;
            both.push_back(device);
        }
        std::vector<VkPhysicalDevice> chosen = discrete.empty() ? integrated : discrete;
        if (all_kinds && !discrete.empty()) chosen.insert(chosen.end(), integrated.begin(), integrated.end());
        if (only) {
            chosen.clear();
            for (const int index : *only) {
                if (index >= 0 && static_cast<std::size_t>(index) < both.size()) chosen.push_back(both[static_cast<std::size_t>(index)]);
            }
        }
        for (VkPhysicalDevice device : chosen) open(device);
        if (gpus_.empty()) throw std::runtime_error("No GPU with Vulkan compute was found.");
    }

    // Vulkan loaded and an instance made for it.
    static void start(Vulkan& vk, VkInstance& instance) {
        if (!vk.load_library()) throw std::runtime_error("Vulkan isn't installed.");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Bitzkey";
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        check(vk.vkCreateInstance(&info, nullptr, &instance), "creating an instance");
        vk.load_instance(instance);
    }

    static std::vector<VkPhysicalDevice> list(Vulkan& vk, VkInstance instance) {
        std::uint32_t count = 0;
        check(vk.vkEnumeratePhysicalDevices(instance, &count, nullptr), "listing GPUs");
        std::vector<VkPhysicalDevice> physical(count);
        check(vk.vkEnumeratePhysicalDevices(instance, &count, physical.data()), "listing GPUs");
        return physical;
    }

    static DeviceInfo describe(Vulkan& vk, VkPhysicalDevice physical) {
        VkPhysicalDeviceProperties properties;
        vk.vkGetPhysicalDeviceProperties(physical, &properties);
        VkPhysicalDeviceMemoryProperties memory{};
        vk.vkGetPhysicalDeviceMemoryProperties(physical, &memory);
        DeviceInfo described;
        described.name = properties.deviceName;
        described.discrete = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        for (std::uint32_t heap = 0; heap < memory.memoryHeapCount; ++heap) {
            if (memory.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) described.memory += memory.memoryHeaps[heap].size;
        }
        return described;
    }

    ~VulkanEngine() override {
        if (!profile_.empty() && !environment("BITZKEY_GPU_PROFILE").empty()) {
            for (const auto& [name, ms] : profile_) std::fprintf(stderr, "%-24s %10.1f ms\n", name.c_str(), ms);
        }
        for (auto& gpu : gpus_) {
            vk_.vkDeviceWaitIdle(gpu->device);
            for (auto& [shape, pool] : gpu->pools) {
                destroy(*gpu, pool.weights);
                destroy(*gpu, pool.traces);
            }
            for (Buffer* buffer : {&gpu->slots, &gpu->inputs, &gpu->outputs, &gpu->groups, &gpu->lessons, &gpu->staging}) destroy(*gpu, *buffer);
            vk_.vkDestroyPipeline(gpu->device, gpu->values, nullptr);
            vk_.vkDestroyPipeline(gpu->device, gpu->learn, nullptr);
            vk_.vkDestroyPipelineLayout(gpu->device, gpu->layout, nullptr);
            vk_.vkDestroyDescriptorSetLayout(gpu->device, gpu->set_layout, nullptr);
            vk_.vkDestroyDescriptorPool(gpu->device, gpu->descriptors, nullptr);
            vk_.vkDestroyFence(gpu->device, gpu->fence, nullptr);
            vk_.vkDestroyCommandPool(gpu->device, gpu->commands, nullptr);
            vk_.vkDestroyDevice(gpu->device, nullptr);
        }
        if (instance_) vk_.vkDestroyInstance(instance_, nullptr);
    }

    const std::vector<DeviceInfo>& devices() const override { return infos_; }
    std::size_t attached() const override { return attached_; }

    Handle attach(const Brain& brain) override {
        // Shared out among the GPUs: each new brain goes to the one holding fewest.
        std::size_t least = 0;
        for (std::size_t index = 1; index < gpus_.size(); ++index) {
            if (gpus_[index]->held < gpus_[least]->held) least = index;
        }
        Gpu& gpu = *gpus_[least];
        Pool& pool = pool_for(gpu, shape_of(brain));
        std::uint32_t slot;
        if (!pool.free.empty()) {
            slot = pool.free.back();
            pool.free.pop_back();
        } else {
            if (pool.next == pool.capacity) grow(gpu, pool);
            slot = pool.next++;
        }
        Upload upload{shape_of(brain), slot, std::vector<float>(pool.slot_floats), std::vector<float>(pool.slot_floats)};
        pack(brain, upload.weights.data(), upload.traces.data());
        gpu.uploads.push_back(std::move(upload));
        ++gpu.held;
        ++attached_;
        Handle handle;
        if (!free_handles_.empty()) {
            handle = free_handles_.back();
            free_handles_.pop_back();
        } else {
            handle = static_cast<Handle>(entries_.size());
            entries_.emplace_back();
        }
        entries_[static_cast<std::size_t>(handle)] = {static_cast<int>(least), shape_of(brain), slot, true};
        return handle;
    }

    void detach(Handle handle) override {
        Entry& entry = entries_.at(static_cast<std::size_t>(handle));
        if (!entry.live) return;
        Gpu& gpu = *gpus_[static_cast<std::size_t>(entry.gpu)];
        gpu.pools.at(entry.shape).free.push_back(entry.slot);
        --gpu.held;
        --attached_;
        entry.live = false;
        free_handles_.push_back(handle);
    }

    void download(std::span<const Handle> brains, std::span<Brain* const> into) override {
        flush_all();
        for (std::size_t g = 0; g < gpus_.size(); ++g) {
            Gpu& gpu = *gpus_[g];
            std::vector<std::size_t> mine;
            std::size_t floats = 0;
            for (std::size_t index = 0; index < brains.size(); ++index) {
                const Entry& entry = entries_.at(static_cast<std::size_t>(brains[index]));
                if (entry.gpu != static_cast<int>(g)) continue;
                mine.push_back(index);
                floats += static_cast<std::size_t>(slot_floats(entry.shape));
            }
            if (mine.empty()) continue;
            ensure(gpu, gpu.staging, floats * sizeof(float), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            begin(gpu);
            VkDeviceSize offset = 0;
            for (const std::size_t index : mine) {
                const Entry& entry = entries_[static_cast<std::size_t>(brains[index])];
                const Pool& pool = gpu.pools.at(entry.shape);
                const VkDeviceSize bytes = pool.slot_floats * sizeof(float);
                const VkBufferCopy weights{entry.slot * bytes, offset, bytes};
                vk_.vkCmdCopyBuffer(gpu.cmd, pool.weights.buffer, gpu.staging.buffer, 1, &weights);
                offset += bytes;
            }
            submit_and_wait(gpu);
            const float* data = static_cast<const float*>(gpu.staging.mapped);
            for (const std::size_t index : mine) {
                Brain& brain = *into[index];
                const auto [depth, width] = entries_[static_cast<std::size_t>(brains[index])].shape;
                brain.layers.clear();
                for (int layer = 0; layer < depth; ++layer) brain.layers.push_back(Layer::zeros(width, layer == 0 ? INPUT_SIZE : width));
                for (auto& row : brain.output_weights) row.assign(static_cast<std::size_t>(width), 0.0);
                unpack_parts(data, brain.layers, brain.output_weights, brain.output_biases);
                data += slot_floats({depth, width});
            }
        }
    }

    void values(std::span<const Handle> brains, std::span<const Senses> senses, std::span<Values> out) override {
        auto clock = std::chrono::steady_clock::now();
        flush_all();
        lap("values: new brains up", clock);
        std::vector<Batch> batches(gpus_.size());
        for (std::size_t index = 0; index < brains.size(); ++index) {
            const Entry& entry = entries_.at(static_cast<std::size_t>(brains[index]));
            batches[static_cast<std::size_t>(entry.gpu)].by_shape[entry.shape].push_back(index);
        }
        for (std::size_t g = 0; g < gpus_.size(); ++g) {  // every GPU set working, then all waited on
            Gpu& gpu = *gpus_[g];
            Batch& batch = batches[g];
            std::size_t count = 0;
            for (const auto& [shape, members] : batch.by_shape) count += members.size();
            if (!count) continue;
            ensure(gpu, gpu.slots, count * sizeof(std::uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            ensure(gpu, gpu.inputs, count * INPUT_SIZE * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            ensure(gpu, gpu.outputs, count * OUTPUT_COUNT * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            auto* slots = static_cast<std::uint32_t*>(gpu.slots.mapped);
            auto* inputs = static_cast<float*>(gpu.inputs.mapped);
            begin(gpu);
            vk_.vkResetDescriptorPool(gpu.device, gpu.descriptors, 0);
            std::uint32_t first = 0;
            for (const auto& [shape, members] : batch.by_shape) {
                for (std::size_t k = 0; k < members.size(); ++k) {
                    const std::size_t index = members[k];
                    slots[first + k] = entries_[static_cast<std::size_t>(brains[index])].slot;
                    for (int sense = 0; sense < INPUT_SIZE; ++sense) {
                        inputs[(first + k) * INPUT_SIZE + static_cast<std::size_t>(sense)] = static_cast<float>(read_sense(sense, senses[index][sense]));
                    }
                }
                const Pool& pool = gpu.pools.at(shape);
                dispatch(gpu, gpu.values, {&pool.weights, &gpu.slots, &gpu.inputs, &gpu.outputs, &gpu.outputs}, shape,
                         static_cast<std::uint32_t>(members.size()), pool.slot_floats, first);
                first += static_cast<std::uint32_t>(members.size());
            }
            submit(gpu);
        }
        lap("values: pack and send", clock);
        for (std::size_t g = 0; g < gpus_.size(); ++g) {
            Gpu& gpu = *gpus_[g];
            if (batches[g].by_shape.empty()) continue;
            wait(gpu);
            lap("values: GPU", clock);
            const auto* values = static_cast<const float*>(gpu.outputs.mapped);
            std::size_t at = 0;
            for (const auto& [shape, members] : batches[g].by_shape) {
                for (const std::size_t index : members) {
                    for (int output = 0; output < OUTPUT_COUNT; ++output) out[index][output] = values[at * OUTPUT_COUNT + static_cast<std::size_t>(output)];
                    ++at;
                }
            }
        }
        lap("values: read back", clock);
    }

    void learn(std::span<const Lesson> lessons) override {
        if (lessons.empty()) return;
        auto clock = std::chrono::steady_clock::now();
        flush_all();
        lap("learn: new brains up", clock);
        // Each brain's lessons together, in the order given; brains grouped by GPU and shape.
        std::unordered_map<Handle, std::vector<std::size_t>> by_brain;
        std::vector<Handle> order;
        for (std::size_t index = 0; index < lessons.size(); ++index) {
            auto [found, fresh] = by_brain.try_emplace(lessons[index].brain);
            if (fresh) order.push_back(lessons[index].brain);
            found->second.push_back(index);
        }
        lap("learn: group", clock);
        std::vector<std::map<Shape, std::vector<Handle>>> brains_by(gpus_.size());
        for (const Handle handle : order) {
            const Entry& entry = entries_.at(static_cast<std::size_t>(handle));
            brains_by[static_cast<std::size_t>(entry.gpu)][entry.shape].push_back(handle);
        }
        for (std::size_t g = 0; g < gpus_.size(); ++g) {
            Gpu& gpu = *gpus_[g];
            std::size_t groups = 0, count = 0;
            for (const auto& [shape, handles] : brains_by[g]) {
                groups += handles.size();
                for (const Handle handle : handles) count += by_brain[handle].size();
            }
            if (!groups) continue;
            ensure(gpu, gpu.groups, groups * sizeof(GpuGroup), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            ensure(gpu, gpu.lessons, count * sizeof(GpuLesson), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            ensure(gpu, gpu.inputs, count * INPUT_SIZE * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            auto* group_data = static_cast<GpuGroup*>(gpu.groups.mapped);
            auto* lesson_data = static_cast<GpuLesson*>(gpu.lessons.mapped);
            auto* inputs = static_cast<float*>(gpu.inputs.mapped);
            begin(gpu);
            vk_.vkResetDescriptorPool(gpu.device, gpu.descriptors, 0);
            // Where each brain's group and lessons go, then all of them written at once, on every core.
            struct Place {
                Handle handle;
                std::uint32_t group, lesson;
            };
            std::vector<Place> places;
            places.reserve(groups);
            std::vector<std::pair<Shape, std::uint32_t>> starts;  // each shape's first group
            std::uint32_t group_at = 0, lesson_at = 0;
            for (const auto& [shape, handles] : brains_by[g]) {
                starts.emplace_back(shape, group_at);
                for (const Handle handle : handles) {
                    places.push_back({handle, group_at++, lesson_at});
                    lesson_at += static_cast<std::uint32_t>(by_brain[handle].size());
                }
            }
            std::for_each(std::execution::par, places.begin(), places.end(), [&](const Place& place) {
                const std::vector<std::size_t>& mine = by_brain.at(place.handle);
                group_data[place.group] = {entries_[static_cast<std::size_t>(place.handle)].slot, place.lesson,
                                           static_cast<std::uint32_t>(mine.size()), 0};
                std::uint32_t at = place.lesson;
                for (const std::size_t index : mine) {
                    const Lesson& lesson = lessons[index];
                    const std::uint32_t kind = lesson.steps > 0 ? 2u : lesson.steps < 0 ? 3u : lesson.traced ? 1u : 0u;
                    lesson_data[at] = {static_cast<std::uint32_t>(lesson.action), kind, static_cast<float>(lesson.target),
                                       static_cast<float>(lesson.rate), static_cast<float>(lesson.decay), static_cast<float>(LESSON_LIMIT),
                                       static_cast<std::uint32_t>(std::max(0, lesson.steps)), 0};
                    float* into = inputs + static_cast<std::size_t>(at) * INPUT_SIZE;
                    for (int sense = 0; sense < INPUT_SIZE; ++sense) into[sense] = static_cast<float>(read_sense(sense, (*lesson.senses)[sense]));
                    ++at;
                }
            });
            for (std::size_t k = 0; k < starts.size(); ++k) {
                const auto [shape, first_group] = starts[k];
                const std::uint32_t last = k + 1 < starts.size() ? starts[k + 1].second : group_at;
                const Pool& pool = gpu.pools.at(shape);
                dispatch(gpu, gpu.learn, {&pool.weights, &pool.traces, &gpu.groups, &gpu.lessons, &gpu.inputs}, shape, last - first_group,
                         pool.slot_floats, first_group);
            }
            submit(gpu);
        }
        lap("learn: pack and send", clock);
        for (std::size_t g = 0; g < gpus_.size(); ++g) {
            if (!brains_by[g].empty()) wait(*gpus_[g]);
        }
        lap("learn: GPU", clock);
    }

private:
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        void* mapped = nullptr;  // host-visible buffers only
    };
    struct Pool {
        std::uint32_t slot_floats = 0;
        std::uint32_t capacity = 0;
        std::uint32_t next = 0;  // slots used so far, free ones aside
        std::vector<std::uint32_t> free;
        Buffer weights, traces;  // on the GPU itself
    };
    struct Upload {
        Shape shape;
        std::uint32_t slot;
        std::vector<float> weights, traces;
    };
    struct Gpu {
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties memory{};
        std::uint32_t max_groups = 65535;
        VkCommandPool commands = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkDescriptorPool descriptors = VK_NULL_HANDLE;
        VkPipeline values = VK_NULL_HANDLE, learn = VK_NULL_HANDLE;
        std::map<Shape, Pool> pools;  // by shape
        Buffer slots, inputs, outputs, groups, lessons, staging;  // host-visible, grown as needed
        std::vector<Upload> uploads;  // brains attached, not yet copied up
        std::size_t held = 0;
    };
    struct Entry {
        int gpu = 0;
        Shape shape{};
        std::uint32_t slot = 0;
        bool live = false;
    };
    struct Batch {
        std::map<Shape, std::vector<std::size_t>> by_shape;
    };

    void open(VkPhysicalDevice physical) {
        auto gpu = std::make_unique<Gpu>();
        gpu->physical = physical;
        VkPhysicalDeviceProperties properties;
        vk_.vkGetPhysicalDeviceProperties(physical, &properties);
        vk_.vkGetPhysicalDeviceMemoryProperties(physical, &gpu->memory);
        gpu->max_groups = std::max<std::uint32_t>(1, properties.limits.maxComputeWorkGroupCount[0]);
        std::uint32_t families = 0;
        vk_.vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, nullptr);
        std::vector<VkQueueFamilyProperties> family(families);
        vk_.vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, family.data());
        std::uint32_t compute = families;
        for (std::uint32_t index = 0; index < families; ++index) {
            if (family[index].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                compute = index;
                break;
            }
        }
        if (compute == families) return;  // no compute queue: not a GPU we can use
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = compute;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queue;
        check(vk_.vkCreateDevice(physical, &info, nullptr, &gpu->device), "opening a GPU");
        vk_.vkGetDeviceQueue(gpu->device, compute, 0, &gpu->queue);

        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = compute;
        check(vk_.vkCreateCommandPool(gpu->device, &pool, nullptr, &gpu->commands), "making a command pool");
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = gpu->commands;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        check(vk_.vkAllocateCommandBuffers(gpu->device, &allocate, &gpu->cmd), "making a command buffer");
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        check(vk_.vkCreateFence(gpu->device, &fence, nullptr, &gpu->fence), "making a fence");

        VkDescriptorSetLayoutBinding bindings[5];
        for (std::uint32_t binding = 0; binding < 5; ++binding) {
            bindings[binding] = {binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
        }
        VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        set.bindingCount = 5;
        set.pBindings = bindings;
        check(vk_.vkCreateDescriptorSetLayout(gpu->device, &set, nullptr, &gpu->set_layout), "laying out descriptors");
        VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &gpu->set_layout;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &push;
        check(vk_.vkCreatePipelineLayout(gpu->device, &layout, nullptr, &gpu->layout), "laying out a pipeline");
        VkDescriptorPoolSize sizes{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5 * 64};
        VkDescriptorPoolCreateInfo descriptors{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        descriptors.maxSets = 64;
        descriptors.poolSizeCount = 1;
        descriptors.pPoolSizes = &sizes;
        check(vk_.vkCreateDescriptorPool(gpu->device, &descriptors, nullptr, &gpu->descriptors), "making a descriptor pool");
        gpu->values = pipeline(*gpu, VALUES_SPIRV, sizeof(VALUES_SPIRV));
        gpu->learn = pipeline(*gpu, LEARN_SPIRV, sizeof(LEARN_SPIRV));

        infos_.push_back(describe(vk_, physical));
        gpus_.push_back(std::move(gpu));
    }

    VkPipeline pipeline(Gpu& gpu, const std::uint32_t* code, std::size_t bytes) {
        VkShaderModuleCreateInfo module_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        module_info.codeSize = bytes;
        module_info.pCode = code;
        VkShaderModule module;
        check(vk_.vkCreateShaderModule(gpu.device, &module_info, nullptr, &module), "loading a shader");
        VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        info.stage.module = module;
        info.stage.pName = "main";
        info.layout = gpu.layout;
        VkPipeline made;
        const VkResult result = vk_.vkCreateComputePipelines(gpu.device, VK_NULL_HANDLE, 1, &info, nullptr, &made);
        vk_.vkDestroyShaderModule(gpu.device, module, nullptr);
        check(result, "building a pipeline");
        return made;
    }

    std::uint32_t memory_type(const Gpu& gpu, std::uint32_t allowed, VkMemoryPropertyFlags wanted, VkMemoryPropertyFlags preferred = 0) const {
        for (const VkMemoryPropertyFlags flags : {wanted | preferred, wanted}) {
            for (std::uint32_t type = 0; type < gpu.memory.memoryTypeCount; ++type) {
                if ((allowed & (1u << type)) && (gpu.memory.memoryTypes[type].propertyFlags & flags) == flags) return type;
            }
        }
        throw std::runtime_error("Vulkan: no suitable memory");
    }

    Buffer make(Gpu& gpu, VkDeviceSize size, VkBufferUsageFlags usage, bool host) {
        Buffer made;
        made.size = std::max<VkDeviceSize>(size, 16);
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = made.size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vk_.vkCreateBuffer(gpu.device, &info, nullptr, &made.buffer), "making a buffer");
        VkMemoryRequirements needs;
        vk_.vkGetBufferMemoryRequirements(gpu.device, made.buffer, &needs);
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocate.allocationSize = needs.size;
        allocate.memoryTypeIndex = host ? memory_type(gpu, needs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                                      VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
                                        : memory_type(gpu, needs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vk_.vkAllocateMemory(gpu.device, &allocate, nullptr, &made.memory), "allocating GPU memory");
        check(vk_.vkBindBufferMemory(gpu.device, made.buffer, made.memory, 0), "binding a buffer");
        if (host) check(vk_.vkMapMemory(gpu.device, made.memory, 0, VK_WHOLE_SIZE, 0, &made.mapped), "mapping a buffer");
        return made;
    }

    void destroy(Gpu& gpu, Buffer& buffer) {
        if (buffer.buffer) vk_.vkDestroyBuffer(gpu.device, buffer.buffer, nullptr);
        if (buffer.memory) vk_.vkFreeMemory(gpu.device, buffer.memory, nullptr);
        buffer = {};
    }

    void ensure(Gpu& gpu, Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage) {  // a host-visible buffer at least this big
        if (buffer.buffer && buffer.size >= size) return;
        destroy(gpu, buffer);
        buffer = make(gpu, std::max<VkDeviceSize>(size, 1 << 16) * 3 / 2,
                      usage | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
    }

    Pool& pool_for(Gpu& gpu, Shape shape) {
        auto [found, fresh] = gpu.pools.try_emplace(shape);
        if (fresh) found->second.slot_floats = slot_floats(shape);
        return found->second;
    }

    void grow(Gpu& gpu, Pool& pool) {  // twice the room, the brains already there copied across on the GPU
        const std::uint32_t capacity = std::max<std::uint32_t>(64, pool.capacity * 2);
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(capacity) * pool.slot_floats * sizeof(float);
        const VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        Buffer weights = make(gpu, bytes, usage, false), traces = make(gpu, bytes, usage, false);
        if (pool.capacity) {
            begin(gpu);
            const VkBufferCopy copy{0, 0, static_cast<VkDeviceSize>(pool.capacity) * pool.slot_floats * sizeof(float)};
            vk_.vkCmdCopyBuffer(gpu.cmd, pool.weights.buffer, weights.buffer, 1, &copy);
            vk_.vkCmdCopyBuffer(gpu.cmd, pool.traces.buffer, traces.buffer, 1, &copy);
            submit_and_wait(gpu);
        }
        destroy(gpu, pool.weights);
        destroy(gpu, pool.traces);
        pool.weights = weights;
        pool.traces = traces;
        pool.capacity = capacity;
    }

    void flush_all() {
        for (auto& gpu : gpus_) flush(*gpu);
    }

    void flush(Gpu& gpu) {  // copy every brain attached since last time up to its pool
        if (gpu.uploads.empty()) return;
        std::size_t floats = 0;
        for (const Upload& upload : gpu.uploads) floats += upload.weights.size() + upload.traces.size();
        ensure(gpu, gpu.staging, floats * sizeof(float), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        auto* data = static_cast<float*>(gpu.staging.mapped);
        begin(gpu);
        VkDeviceSize offset = 0;
        for (const Upload& upload : gpu.uploads) {
            const Pool& pool = gpu.pools.at(upload.shape);
            const VkDeviceSize bytes = pool.slot_floats * sizeof(float);
            std::memcpy(data, upload.weights.data(), bytes);
            std::memcpy(data + pool.slot_floats, upload.traces.data(), bytes);
            data += 2 * pool.slot_floats;
            const VkBufferCopy weights{offset, upload.slot * bytes, bytes};
            const VkBufferCopy traces{offset + bytes, upload.slot * bytes, bytes};
            vk_.vkCmdCopyBuffer(gpu.cmd, gpu.staging.buffer, pool.weights.buffer, 1, &weights);
            vk_.vkCmdCopyBuffer(gpu.cmd, gpu.staging.buffer, pool.traces.buffer, 1, &traces);
            offset += 2 * bytes;
        }
        submit_and_wait(gpu);
        gpu.uploads.clear();
    }

    void begin(Gpu& gpu) {
        check(vk_.vkResetCommandBuffer(gpu.cmd, 0), "resetting commands");
        VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vk_.vkBeginCommandBuffer(gpu.cmd, &info), "recording commands");
    }

    void submit(Gpu& gpu) {
        // Whatever the GPU wrote is made visible to the CPU before it reads it back.
        VkMemoryBarrier visible{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        visible.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        visible.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vk_.vkCmdPipelineBarrier(gpu.cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &visible, 0, nullptr, 0,
                                 nullptr);
        check(vk_.vkEndCommandBuffer(gpu.cmd), "recording commands");
        VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        info.commandBufferCount = 1;
        info.pCommandBuffers = &gpu.cmd;
        check(vk_.vkResetFences(gpu.device, 1, &gpu.fence), "resetting a fence");
        check(vk_.vkQueueSubmit(gpu.queue, 1, &info, gpu.fence), "submitting work");
    }

    void wait(Gpu& gpu) { check(vk_.vkWaitForFences(gpu.device, 1, &gpu.fence, VK_TRUE, UINT64_MAX), "waiting for the GPU"); }

    void submit_and_wait(Gpu& gpu) {
        submit(gpu);
        wait(gpu);
    }

    void dispatch(Gpu& gpu, VkPipeline pipe, std::initializer_list<const Buffer*> buffers, Shape shape, std::uint32_t count,
                  std::uint32_t floats, std::uint32_t first) {
        if (!count) return;
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = gpu.descriptors;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &gpu.set_layout;
        VkDescriptorSet set;
        check(vk_.vkAllocateDescriptorSets(gpu.device, &allocate, &set), "allocating descriptors");
        VkDescriptorBufferInfo infos[5];
        VkWriteDescriptorSet writes[5];
        std::uint32_t binding = 0;
        for (const Buffer* buffer : buffers) {
            infos[binding] = {buffer->buffer, 0, VK_WHOLE_SIZE};
            writes[binding] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            writes[binding].dstSet = set;
            writes[binding].dstBinding = binding;
            writes[binding].descriptorCount = 1;
            writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[binding].pBufferInfo = &infos[binding];
            ++binding;
        }
        vk_.vkUpdateDescriptorSets(gpu.device, binding, writes, 0, nullptr);
        vk_.vkCmdBindPipeline(gpu.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
        vk_.vkCmdBindDescriptorSets(gpu.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, gpu.layout, 0, 1, &set, 0, nullptr);
        for (std::uint32_t done = 0; done < count; done += gpu.max_groups) {  // in as many dispatches as the GPU needs
            const std::uint32_t part = std::min(gpu.max_groups, count - done);
            const Push push{static_cast<std::uint32_t>(shape.first), part, floats, first + done, static_cast<std::uint32_t>(shape.second)};
            vk_.vkCmdPushConstants(gpu.cmd, gpu.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
            vk_.vkCmdDispatch(gpu.cmd, part, 1, 1);
        }
    }

    void lap(const char* name, std::chrono::steady_clock::time_point& since) {  // where the time goes: BITZKEY_GPU_PROFILE
        const auto now = std::chrono::steady_clock::now();
        profile_[name] += std::chrono::duration<double, std::milli>(now - since).count();
        since = now;
    }

    Vulkan vk_;
    std::map<std::string, double> profile_;
    VkInstance instance_ = VK_NULL_HANDLE;
    std::vector<std::unique_ptr<Gpu>> gpus_;
    std::vector<DeviceInfo> infos_;
    std::vector<Entry> entries_;
    std::vector<Handle> free_handles_;
    std::size_t attached_ = 0;
};

}  // namespace


bool available(std::string* why) {
    static const std::pair<bool, std::string> found = [] {
        std::string reason;
        const bool usable = BrainEngine::create(&reason) != nullptr;
        return std::pair{usable, reason};
    }();
    if (why) *why = found.second;
    return found.first;
}

std::unique_ptr<BrainEngine> BrainEngine::create(std::string* why) {
    const std::string choice = environment("BITZKEY_GPU");
    if (choice == "off" || choice == "0" || choice == "none") {
        if (why) *why = "GPUs turned off (BITZKEY_GPU=off).";
        return nullptr;
    }
    try {
        return std::make_unique<VulkanEngine>(choice == "all");
    } catch (const std::exception& error) {
        if (why) *why = error.what();
        return nullptr;
    }
}

std::unique_ptr<BrainEngine> BrainEngine::create_on(const std::vector<int>& chosen, std::string* why) {
    try {
        return std::make_unique<VulkanEngine>(false, &chosen);
    } catch (const std::exception& error) {
        if (why) *why = error.what();
        return nullptr;
    }
}

std::vector<DeviceInfo> scan() {
    std::vector<DeviceInfo> found;
    try {
        Vulkan vk;
        VkInstance instance = VK_NULL_HANDLE;
        VulkanEngine::start(vk, instance);
        for (VkPhysicalDevice device : VulkanEngine::list(vk, instance)) {
            VkPhysicalDeviceProperties properties;
            vk.vkGetPhysicalDeviceProperties(device, &properties);
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
                found.push_back(VulkanEngine::describe(vk, device));
            }
        }
        vk.vkDestroyInstance(instance, nullptr);
    } catch (const std::exception&) {  // no Vulkan: no GPUs
    }
    return found;
}

}  // namespace bitzkey::gpu
