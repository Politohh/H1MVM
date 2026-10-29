#include <std_include.hpp>

#include "dxvk_probe.hpp"

#include <chrono>
#include <cstring>
#include <vector>

// The checks DXVK 3.0.2 makes before it accepts an adapter (DxvkDeviceCapabilities::
// checkDeviceCompatibility in src/dxvk/dxvk_device_info.cpp): Vulkan 1.4, a graphics queue, the
// required extensions and features. A GPU that fails any of them would make D3D11CreateDevice
// fail on DXVK, so the game has to start on native D3D11 instead.
//
// There are no Vulkan headers in this tree, so the few structures used are declared here. Every
// structure the driver writes into is a block with room to spare: a field this file doesn't name
// can make an answer wrong, never overrun memory.
namespace dxvk_probe
{
	namespace
	{
		using vk_result = std::int32_t;
		using vk_instance = struct vk_instance_t*;
		using vk_physical_device = struct vk_physical_device_t*;
		using vk_void_function = void(*)();

		constexpr std::uint32_t make_version(const std::uint32_t major, const std::uint32_t minor)
		{
			return (major << 22) | (minor << 12);
		}

		constexpr auto required_api = make_version(1, 4);

		constexpr std::uint32_t type_application_info = 0;
		constexpr std::uint32_t type_instance_create_info = 1;
		constexpr std::uint32_t type_features_2 = 1000059000;
		constexpr std::uint32_t type_vulkan_11_features = 49;
		constexpr std::uint32_t type_vulkan_12_features = 51;
		constexpr std::uint32_t type_vulkan_13_features = 53;
		constexpr std::uint32_t type_robustness_2_features = 1000286000;
		constexpr std::uint32_t type_depth_clip_enable_features = 1000102000;

		struct application_info
		{
			std::uint32_t type;
			const void* next;
			const char* application_name;
			std::uint32_t application_version;
			const char* engine_name;
			std::uint32_t engine_version;
			std::uint32_t api_version;
		};

		struct instance_create_info
		{
			std::uint32_t type;
			const void* next;
			std::uint32_t flags;
			const application_info* application;
			std::uint32_t layer_count;
			const char* const* layers;
			std::uint32_t extension_count;
			const char* const* extensions;
		};

		// VkPhysicalDeviceProperties begins with these; the rest (limits) is only given room
		struct device_properties
		{
			std::uint32_t api_version;
			std::uint32_t driver_version;
			std::uint32_t vendor;
			std::uint32_t device_id;
			std::int32_t device_type;
			char device_name[256];
			std::uint8_t rest[4096];
		};

		struct queue_family
		{
			std::uint32_t flags;
			std::uint32_t count;
			std::uint32_t timestamp_bits;
			std::uint32_t granularity[3];
		};

		struct extension_properties
		{
			char name[256];
			std::uint32_t spec_version;
		};

		// Any VkPhysicalDevice*Features structure: sType, pNext, then VkBool32 fields in order
		struct feature_block
		{
			std::uint32_t type;
			void* next;
			std::uint32_t enabled[160];
		};

		struct required_feature
		{
			int index;
			const char* name;
		};

		// VkPhysicalDeviceFeatures field indices
		constexpr required_feature core_features[]
		{
			{0, "robustBufferAccess"}, {1, "fullDrawIndexUint32"}, {2, "imageCubeArray"}, {3, "independentBlend"},
			{4, "geometryShader"}, {6, "sampleRateShading"}, {7, "dualSrcBlend"}, {9, "multiDrawIndirect"},
			{11, "depthClamp"}, {12, "depthBiasClamp"}, {13, "fillModeNonSolid"}, {18, "multiViewport"},
			{19, "samplerAnisotropy"}, {22, "textureCompressionBC"}, {23, "occlusionQueryPrecise"},
			{26, "fragmentStoresAndAtomics"}, {28, "shaderImageGatherExtended"}, {34, "shaderSampledImageArrayDynamicIndexing"},
			{37, "shaderClipDistance"}, {38, "shaderCullDistance"}, {40, "shaderInt64"}, {41, "shaderInt16"},
		};

		constexpr required_feature vulkan_11_features[]
		{
			{0, "storageBuffer16BitAccess"}, {11, "shaderDrawParameters"},
		};

		constexpr required_feature vulkan_12_features[]
		{
			{0, "samplerMirrorClampToEdge"}, {2, "storageBuffer8BitAccess"}, {8, "shaderInt8"}, {9, "descriptorIndexing"},
			{21, "descriptorBindingSampledImageUpdateAfterBind"}, {26, "descriptorBindingUpdateUnusedWhilePending"},
			{27, "descriptorBindingPartiallyBound"}, {29, "runtimeDescriptorArray"}, {31, "scalarBlockLayout"},
			{33, "uniformBufferStandardLayout"}, {36, "hostQueryReset"}, {37, "timelineSemaphore"},
			{38, "bufferDeviceAddress"}, {41, "vulkanMemoryModel"},
		};

		constexpr required_feature vulkan_13_features[]
		{
			{1, "inlineUniformBlock"}, {5, "shaderDemoteToHelperInvocation"}, {7, "subgroupSizeControl"},
			{8, "computeFullSubgroups"}, {9, "synchronization2"}, {11, "shaderZeroInitializeWorkgroupMemory"},
			{12, "dynamicRendering"}, {14, "maintenance4"},
		};

		constexpr required_feature robustness_2_features[]
		{
			{0, "robustBufferAccess2"}, {2, "nullDescriptor"},
		};

		constexpr required_feature depth_clip_features[]
		{
			{0, "depthClipEnable"},
		};

		// The order DXVK lists adapters in: discrete, integrated, virtual, other. Never a CPU device.
		int device_rank(const std::int32_t type)
		{
			switch (type)
			{
			case 2: return 3;
			case 1: return 2;
			case 3: return 1;
			case 0: return 0;
			default: return -1;
			}
		}

		std::string version_string(const std::uint32_t version)
		{
			return std::to_string(version >> 22) + "." + std::to_string((version >> 12) & 0x3FF);
		}

		template <std::size_t Count>
		const char* missing(const feature_block& block, const required_feature (&features)[Count])
		{
			for (const auto& feature : features)
			{
				if (!block.enabled[feature.index])
				{
					return feature.name;
				}
			}

			return nullptr;
		}

		struct vulkan
		{
			HMODULE library{};
			vk_instance instance{};
			vk_void_function(*get_instance_proc)(vk_instance, const char*){};

			template <typename T>
			T get(const char* name) const
			{
				return reinterpret_cast<T>(this->get_instance_proc(this->instance, name));
			}

			~vulkan()
			{
				if (this->instance)
				{
					if (const auto destroy = this->get<void(*)(vk_instance, const void*)>("vkDestroyInstance"))
					{
						destroy(this->instance, nullptr);
					}
				}

				if (this->library)
				{
					FreeLibrary(this->library);
				}
			}
		};

		void check(result& out)
		{
			vulkan vk{};
			// By name, like DXVK, so both see the same loader
			vk.library = LoadLibraryW(L"vulkan-1.dll");
			if (!vk.library)
			{
				out.reason = "no Vulkan driver (vulkan-1.dll not found)";
				return;
			}

			vk.get_instance_proc = reinterpret_cast<decltype(vk.get_instance_proc)>(GetProcAddress(vk.library, "vkGetInstanceProcAddr"));
			if (!vk.get_instance_proc)
			{
				out.reason = "vulkan-1.dll has no vkGetInstanceProcAddr";
				return;
			}

			const auto create_instance = vk.get<vk_result(*)(const instance_create_info*, const void*, vk_instance*)>("vkCreateInstance");
			const application_info application{type_application_info, nullptr, "h1-mod dxvk probe", 1, "h1-mod", 1, required_api};
			const instance_create_info create_info{type_instance_create_info, nullptr, 0, &application, 0, nullptr, 0, nullptr};
			if (!create_instance || create_instance(&create_info, nullptr, &vk.instance) != 0 || !vk.instance)
			{
				out.reason = "the Vulkan driver refused to create an instance";
				vk.instance = nullptr;
				return;
			}

			const auto enumerate_devices = vk.get<vk_result(*)(vk_instance, std::uint32_t*, vk_physical_device*)>("vkEnumeratePhysicalDevices");
			const auto get_properties = vk.get<void(*)(vk_physical_device, device_properties*)>("vkGetPhysicalDeviceProperties");
			const auto get_queues = vk.get<void(*)(vk_physical_device, std::uint32_t*, queue_family*)>("vkGetPhysicalDeviceQueueFamilyProperties");
			const auto get_extensions = vk.get<vk_result(*)(vk_physical_device, const char*, std::uint32_t*, extension_properties*)>("vkEnumerateDeviceExtensionProperties");
			const auto get_features = vk.get<void(*)(vk_physical_device, feature_block*)>("vkGetPhysicalDeviceFeatures2");
			if (!enumerate_devices || !get_properties || !get_queues || !get_extensions || !get_features)
			{
				out.reason = "the Vulkan driver is missing basic entry points";
				return;
			}

			std::uint32_t count = 0;
			enumerate_devices(vk.instance, &count, nullptr);
			std::vector<vk_physical_device> devices(count);
			if (!count || enumerate_devices(vk.instance, &count, devices.data()) < 0)
			{
				out.reason = "the Vulkan driver lists no GPU";
				return;
			}

			devices.resize(count);
			auto best = -1;
			vk_physical_device device{};
			auto properties = std::make_unique<device_properties>();
			for (const auto candidate : devices)
			{
				std::memset(properties.get(), 0, sizeof(device_properties));
				get_properties(candidate, properties.get());
				if (const auto rank = device_rank(properties->device_type); rank > best)
				{
					best = rank;
					device = candidate;
				}
			}

			if (!device)
			{
				out.reason = "the only Vulkan devices are software renderers";
				return;
			}

			std::memset(properties.get(), 0, sizeof(device_properties));
			get_properties(device, properties.get());
			properties->device_name[sizeof(properties->device_name) - 1] = 0;
			out.device = properties->device_name;
			out.vendor = properties->vendor;
			out.device_id = properties->device_id;
			out.driver_version = properties->driver_version;
			out.api_version = properties->api_version;

			if (properties->api_version < required_api)
			{
				out.reason = "the driver supports Vulkan " + version_string(properties->api_version) + "; DXVK 3 needs 1.4";
				return;
			}

			std::uint32_t families = 0;
			get_queues(device, &families, nullptr);
			std::vector<queue_family> queues(families);
			get_queues(device, &families, queues.data());
			if (std::none_of(queues.begin(), queues.begin() + families, [](const queue_family& family) { return family.flags & 1; }))
			{
				out.reason = "the GPU has no graphics queue";
				return;
			}

			std::uint32_t extension_count = 0;
			get_extensions(device, nullptr, &extension_count, nullptr);
			std::vector<extension_properties> extensions(extension_count);
			get_extensions(device, nullptr, &extension_count, extensions.data());
			const auto has = [&](const char* name)
			{
				return std::any_of(extensions.begin(), extensions.begin() + extension_count,
					[&](const extension_properties& extension) { return std::strncmp(extension.name, name, sizeof(extension.name)) == 0; });
			};

			for (const auto* name : {"VK_KHR_swapchain", "VK_EXT_depth_clip_enable"})
			{
				if (!has(name))
				{
					out.reason = std::string("the driver lacks ") + name;
					return;
				}
			}

			if (!has("VK_EXT_robustness2") && !has("VK_KHR_robustness2"))
			{
				out.reason = "the driver lacks VK_EXT_robustness2";
				return;
			}

			// DXVK 3.0's release notes: AMD's Windows driver for RDNA1/RDNA2 no longer gets new
			// features, so DXVK falls back to its legacy binding model there and runs badly.
			if (properties->vendor == 0x1002 && !has("VK_EXT_descriptor_heap"))
			{
				out.reason = "AMD driver without VK_EXT_descriptor_heap (RDNA1/RDNA2 on Windows), where DXVK 3 runs badly";
				return;
			}

			feature_block depth_clip{type_depth_clip_enable_features, nullptr, {}};
			feature_block robustness{type_robustness_2_features, &depth_clip, {}};
			feature_block v13{type_vulkan_13_features, &robustness, {}};
			feature_block v12{type_vulkan_12_features, &v13, {}};
			feature_block v11{type_vulkan_11_features, &v12, {}};
			feature_block core{type_features_2, &v11, {}};
			get_features(device, &core);

			const char* lacking = missing(core, core_features);
			if (!lacking) lacking = missing(v11, vulkan_11_features);
			if (!lacking) lacking = missing(v12, vulkan_12_features);
			if (!lacking) lacking = missing(v13, vulkan_13_features);
			if (!lacking) lacking = missing(robustness, robustness_2_features);
			if (!lacking) lacking = missing(depth_clip, depth_clip_features);
			if (lacking)
			{
				out.reason = std::string("the GPU lacks the Vulkan feature ") + lacking;
				return;
			}

			out.suitable = true;
		}
	}

	result run()
	{
		const auto start = std::chrono::steady_clock::now();
		result out{};
		check(out);
		out.milliseconds = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - start).count());
		return out;
	}
}
