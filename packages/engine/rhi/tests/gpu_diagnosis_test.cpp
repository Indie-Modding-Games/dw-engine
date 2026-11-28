#include <dwe/rhi/gpu_diagnosis.hpp>

#include <doctest/doctest.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using namespace dwe::rhi;

namespace {

constexpr std::uint64_t GiB = 1024ULL * 1024ULL * 1024ULL;

std::uint32_t nvidiaVersion(std::uint32_t major, std::uint32_t minor, std::uint32_t patch = 0) {
  return (major << 22U) | (minor << 14U) | (patch << 6U);
}

DeviceFacts gpu(std::string name, std::uint32_t vendor, GpuKind kind, std::uint32_t api,
                std::uint32_t driverId, std::uint32_t driverVersion) {
  return {.gpu = GpuInfo{.name = std::move(name),
                         .vendorId = vendor,
                         .kind = kind,
                         .apiVersion = api,
                         .driver = "test"},
          .deviceId = 0x1234,
          .driverVersion = driverVersion,
          .driverId = driverId,
          .vramBytes = 4 * GiB,
          .caps = {}};
}

DeviceFacts rtx2060() {
  return gpu("NVIDIA GeForce RTX 2060", 0x10de, GpuKind::Discrete, VK_API_VERSION_1_4,
             VK_DRIVER_ID_NVIDIA_PROPRIETARY, nvidiaVersion(580, 95, 5));
}

DeviceFacts lavapipe() {
  return gpu("llvmpipe (LLVM 20.1.2, 256 bits)", 0x10005, GpuKind::Software, VK_API_VERSION_1_4,
             VK_DRIVER_ID_MESA_LLVMPIPE, VK_MAKE_API_VERSION(0, 25, 2, 8));
}

SystemFacts on(OsKind os) {
  SystemFacts facts;
  facts.os = os;
  return facts;
}

AdapterFacts adapter(std::string name, std::uint32_t vendor = 0, std::uint32_t device = 0,
                     std::string driver = {}) {
  return {
      .name = std::move(name), .vendorId = vendor, .deviceId = device, .driver = std::move(driver)};
}

LayerFacts layer(std::string name) {
  return {.name = std::move(name), .manifest = "layer.json"};
}

SystemFacts noVulkanDriver(OsKind os) {
  SystemFacts facts = on(os);
  facts.instanceCreated = false;
  facts.instanceResult = VK_ERROR_INCOMPATIBLE_DRIVER;
  return facts;
}

std::vector<std::string> codes(const Diagnosis& diagnosis) {
  std::vector<std::string> out;
  for (const Reason& reason : diagnosis.reasons) {
    out.push_back(reason.code);
  }
  return out;
}

const Reason* find(const Diagnosis& diagnosis, std::string_view code) {
  const auto found = std::ranges::find(diagnosis.reasons, code, &Reason::code);
  return found == diagnosis.reasons.end() ? nullptr : &*found;
}

} // namespace

TEST_CASE("driver versions are decoded the way each maker writes them") {
  CHECK(decodeDriverVersion(0x10de, VK_DRIVER_ID_NVIDIA_PROPRIETARY, nvidiaVersion(566, 36),
                            OsKind::Windows) == "566.36");
  CHECK(decodeDriverVersion(0x10de, VK_DRIVER_ID_NVIDIA_PROPRIETARY, nvidiaVersion(580, 95, 5),
                            OsKind::Linux) == "580.95.05");
  CHECK(decodeDriverVersion(0x10de, 0, nvidiaVersion(391, 35), OsKind::Windows) == "391.35");
  CHECK(decodeDriverVersion(0x8086, VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS, (101U << 14U) | 5186U,
                            OsKind::Windows) == "101.5186");
  CHECK(decodeDriverVersion(0x8086, 0, (100U << 14U) | 9466U, OsKind::Windows) == "100.9466");
  CHECK(decodeDriverVersion(0x8086, VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA,
                            VK_MAKE_API_VERSION(0, 24, 2, 8), OsKind::Linux) == "24.2.8");
  CHECK(decodeDriverVersion(0x1002, VK_DRIVER_ID_MESA_RADV, VK_MAKE_API_VERSION(0, 25, 2, 8),
                            OsKind::Linux) == "25.2.8");
  CHECK(decodeDriverVersion(0x1002, VK_DRIVER_ID_AMD_PROPRIETARY, VK_MAKE_API_VERSION(0, 2, 0, 279),
                            OsKind::Windows) == "2.0.279");
}

TEST_CASE("dotted versions compare part by part") {
  CHECK(compareVersions("511.65", "511.65") == 0);
  CHECK(compareVersions("510.47.03", "510.47.3") == 0);
  CHECK(compareVersions("472.12", "511.65") < 0);
  CHECK(compareVersions("101.5186", "101.1660") > 0);
  CHECK(compareVersions("21.3.9", "22.0.0") < 0);
}

TEST_CASE("the first Vulkan 1.3 driver is known per maker and system") {
  CHECK(minimumDriverFor13(0x10de, VK_DRIVER_ID_NVIDIA_PROPRIETARY, OsKind::Windows) == "511.65");
  CHECK(minimumDriverFor13(0x10de, VK_DRIVER_ID_NVIDIA_PROPRIETARY, OsKind::Linux) == "510.47.03");
  CHECK(minimumDriverFor13(0x8086, VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS, OsKind::Windows) ==
        "101.1660");
  CHECK(minimumDriverFor13(0x1002, VK_DRIVER_ID_MESA_RADV, OsKind::Linux) == "22.0.0");
  CHECK_FALSE(minimumDriverFor13(0x1002, VK_DRIVER_ID_AMD_PROPRIETARY, OsKind::Windows));
}

TEST_CASE("GPU families say what the newest driver can reach") {
  CHECK(familySupport(0x8086, "Intel(R) HD Graphics 4000", OsKind::Windows) ==
        GpuFamilySupport::NoVulkan);
  CHECK(familySupport(0x8086, "Intel(R) HD Graphics 4000", OsKind::Linux) ==
        GpuFamilySupport::UpTo12);
  CHECK(familySupport(0x8086, "Intel(R) HD Graphics 5500", OsKind::Windows) ==
        GpuFamilySupport::NoVulkan);
  CHECK(familySupport(0x8086, "Intel(R) UHD Graphics 620", OsKind::Windows) ==
        GpuFamilySupport::Full);
  CHECK(familySupport(0x10de, "NVIDIA GeForce GTX 560", OsKind::Windows) ==
        GpuFamilySupport::NoVulkan);
  CHECK(familySupport(0x10de, "NVIDIA GeForce GTX 760", OsKind::Windows) ==
        GpuFamilySupport::UpTo12);
  CHECK(familySupport(0x10de, "NVIDIA GeForce GTX 750 Ti", OsKind::Windows) ==
        GpuFamilySupport::Full);
  CHECK(familySupport(0x10de, "NVIDIA GeForce RTX 3060", OsKind::Windows) ==
        GpuFamilySupport::Full);
  CHECK(familySupport(0x10de, "NVIDIA GeForce GTX 1060 6GB", OsKind::Windows) ==
        GpuFamilySupport::Full);
  CHECK(familySupport(0x1002, "AMD Radeon HD 6800 Series", OsKind::Windows) ==
        GpuFamilySupport::NoVulkan);
  CHECK(familySupport(0x1002, "AMD Radeon R9 290", OsKind::Windows) == GpuFamilySupport::Unknown);
  CHECK(familySupport(0x1002, "AMD Radeon R9 290", OsKind::Linux) == GpuFamilySupport::Full);
}

TEST_CASE("a healthy graphics card has nothing to explain") {
  SystemFacts facts = on(OsKind::Windows);
  facts.devices = {rtx2060()};
  const Diagnosis diagnosis = diagnose(facts);
  CHECK(diagnosis.chosen == 0);
  CHECK(diagnosis.reasons.empty());
}

TEST_CASE("Windows without the maker's driver runs on the basic display adapter") {
  SystemFacts facts = noVulkanDriver(OsKind::Windows);
  facts.adapters = {adapter("Microsoft Basic Display Adapter", 0x10de, 0x1f08)};
  const Diagnosis diagnosis = diagnose(facts);
  REQUIRE_FALSE(diagnosis.reasons.empty());
  CHECK(diagnosis.reasons[0].code == "basic-display-adapter");
  CHECK(diagnosis.reasons[0].param("vendor") == "nvidia");
  CHECK(diagnosis.reasons[0].fix == Fix::Driver);
}

TEST_CASE("a GPU the system lists without a Vulkan driver is named") {
  SUBCASE("hardware that never had Vulkan on Windows") {
    SystemFacts facts = noVulkanDriver(OsKind::Windows);
    facts.adapters = {adapter("Intel(R) HD Graphics 4000", 0x8086, 0x0166)};
    const Diagnosis diagnosis = diagnose(facts);
    CHECK(codes(diagnosis) == std::vector<std::string>{"hardware-no-vulkan"});
    CHECK(diagnosis.reasons[0].param("adapter") == "Intel(R) HD Graphics 4000");
    CHECK(diagnosis.reasons[0].fix == Fix::Hardware);
  }
  SUBCASE("a card whose driver has no Vulkan") {
    SystemFacts facts = on(OsKind::Linux);
    facts.adapters = {
        adapter("NVIDIA Corporation TU106 [GeForce RTX 2060]", 0x10de, 0x1f08, "nouveau")};
    const Diagnosis diagnosis = diagnose(facts);
    CHECK(codes(diagnosis) == std::vector<std::string>{"no-vendor-driver"});
    CHECK(diagnosis.reasons[0].param("kernel") == "nouveau");
  }
  SUBCASE("nothing known at all") {
    CHECK(codes(diagnose(on(OsKind::Linux))) == std::vector<std::string>{"no-gpu"});
  }
}

TEST_CASE("a remote session and a virtual machine say so first") {
  SystemFacts remote = on(OsKind::Windows);
  remote.adapters = {adapter("Microsoft Remote Display Adapter"),
                     adapter("NVIDIA GeForce RTX 2060", 0x10de, 0x1f08)};
  remote.remoteSession = true;
  CHECK(diagnose(remote).reasons.at(0).code == "remote-session");

  SystemFacts virtualMachine = on(OsKind::Windows);
  virtualMachine.adapters = {adapter("VMware SVGA 3D", 0x15ad, 0x0405)};
  CHECK(codes(diagnose(virtualMachine)) == std::vector<std::string>{"virtual-machine"});
}

TEST_CASE("an old Vulkan version says whether a driver raises it") {
  SystemFacts facts = on(OsKind::Windows);
  facts.devices = {gpu("NVIDIA GeForce GTX 760", 0x10de, GpuKind::Discrete, VK_API_VERSION_1_1, 0,
                       nvidiaVersion(391, 35))};
  Diagnosis diagnosis = diagnose(facts);
  REQUIRE(find(diagnosis, "vulkan-too-old") != nullptr);
  CHECK(find(diagnosis, "vulkan-too-old")->fix == Fix::Driver);
  CHECK(find(diagnosis, "vulkan-too-old")->param("family") == "up-to-1.2");
  CHECK(find(diagnosis, "driver-too-old") == nullptr);

  facts.os = OsKind::Linux;
  facts.devices = {gpu("Intel(R) HD Graphics 4000", 0x8086, GpuKind::Integrated, VK_API_VERSION_1_2,
                       VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA, VK_MAKE_API_VERSION(0, 21, 2, 6))};
  facts.devices[0].caps.dynamicRenderingExtension = false;
  facts.devices[0].caps.descriptorBindingUpdateUnusedWhilePending = false;
  diagnosis = diagnose(facts);
  REQUIRE(find(diagnosis, "missing-features") != nullptr);
  CHECK(find(diagnosis, "missing-features")->fix == Fix::Hardware);
  CHECK(find(diagnosis, "missing-features")->param("features") ==
        "descriptorBindingUpdateUnusedWhilePending, VK_KHR_dynamic_rendering");
}

TEST_CASE("a driver older than the first with Vulkan 1.3 shows both versions") {
  DeviceFacts uhd =
      gpu("Intel(R) UHD Graphics 620", 0x8086, GpuKind::Integrated, VK_API_VERSION_1_2,
          VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS, (100U << 14U) | 9466U);
  uhd.caps.dynamicRenderingExtension = false;
  uhd.caps.synchronization2Extension = false;
  SystemFacts facts = on(OsKind::Windows);
  facts.devices = {uhd};
  const Diagnosis diagnosis = diagnose(facts);
  REQUIRE_FALSE(diagnosis.chosen);
  REQUIRE(diagnosis.reasons.size() >= 2);
  CHECK(diagnosis.reasons[0].code == "driver-too-old");
  CHECK(diagnosis.reasons[0].param("driver") == "100.9466");
  CHECK(diagnosis.reasons[0].param("minimum") == "101.1660");
  CHECK(find(diagnosis, "missing-features")->fix == Fix::Driver);
}

TEST_CASE("the compatible path is explained, with the driver that would lift it") {
  SystemFacts facts = on(OsKind::Windows);
  facts.devices = {gpu("NVIDIA GeForce GTX 1060 6GB", 0x10de, GpuKind::Discrete, VK_API_VERSION_1_2,
                       VK_DRIVER_ID_NVIDIA_PROPRIETARY, nvidiaVersion(457, 30))};
  const Diagnosis diagnosis = diagnose(facts);
  CHECK(diagnosis.chosen == 0);
  CHECK(codes(diagnosis) == std::vector<std::string>{"driver-too-old", "compatible-path"});
  CHECK(diagnosis.reasons[0].severity == Severity::Warning);
  CHECK(diagnosis.reasons[0].param("minimum") == "511.65");
  CHECK(diagnosis.reasons[1].param("features") == "Vulkan 1.3");
}

TEST_CASE("a laptop that draws on the integrated GPU names the unused dedicated one") {
  SystemFacts facts = on(OsKind::Windows);
  facts.devices = {gpu("Intel(R) UHD Graphics 630", 0x8086, GpuKind::Integrated, VK_API_VERSION_1_3,
                       VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS, (101U << 14U) | 5186U)};
  facts.adapters = {adapter("Intel(R) UHD Graphics 630", 0x8086, 0x3e9b),
                    adapter("NVIDIA GeForce GTX 1650", 0x10de, 0x1f91)};
  facts.laptop = true;
  const Diagnosis diagnosis = diagnose(facts);
  CHECK(diagnosis.chosen == 0);
  CHECK(codes(diagnosis) == std::vector<std::string>{"dedicated-gpu-unused", "integrated-gpu"});
  CHECK(diagnosis.reasons[0].param("dedicated") == "NVIDIA GeForce GTX 1650");
  CHECK(diagnosis.reasons[0].param("gpu") == "Intel(R) UHD Graphics 630");
}

TEST_CASE("overlay layers are named when the start fails") {
  SystemFacts facts = on(OsKind::Windows);
  facts.instanceCreated = false;
  facts.instanceResult = VK_ERROR_INITIALIZATION_FAILED;
  facts.implicitLayers = {layer("VK_LAYER_NV_optimus"), layer("VK_LAYER_OBS_HOOK"),
                          layer("VK_LAYER_RTSS")};
  const Diagnosis diagnosis = diagnose(facts);
  CHECK(codes(diagnosis) == std::vector<std::string>{"implicit-layers", "instance-failed"});
  CHECK(diagnosis.reasons[0].param("layers") == "VK_LAYER_OBS_HOOK, VK_LAYER_RTSS");
  CHECK(diagnosis.reasons[1].param("result") == "VK_ERROR_INITIALIZATION_FAILED");

  SystemFacts running = on(OsKind::Windows);
  running.devices = {rtx2060()};
  running.implicitLayers = facts.implicitLayers;
  CHECK(diagnose(running).reasons.empty());
}

TEST_CASE("the game's own start failure becomes a reason") {
  SystemFacts facts = on(OsKind::Windows);
  facts.devices = {rtx2060()};
  facts.devices[0].vramBytes = 2 * GiB;
  facts.start = StartFailure{.stage = StartStage::Device,
                             .result = VK_ERROR_OUT_OF_DEVICE_MEMORY,
                             .message = "vkCreateDevice failed"};
  Diagnosis diagnosis = diagnose(facts);
  REQUIRE(find(diagnosis, "out-of-memory") != nullptr);
  CHECK(find(diagnosis, "out-of-memory")->param("vram") == "2048");

  facts.start =
      StartFailure{.stage = StartStage::Running, .result = VK_ERROR_DEVICE_LOST, .message = "lost"};
  diagnosis = diagnose(facts);
  CHECK(codes(diagnosis) == std::vector<std::string>{"device-lost"});
  CHECK(diagnosis.reasons[0].param("driver") == "580.95.05");

  facts.start = StartFailure{
      .stage = StartStage::Window, .result = 0, .message = "No available video device"};
  facts.os = OsKind::Linux;
  facts.displayAvailable = false;
  diagnosis = diagnose(facts);
  CHECK(diagnosis.reasons.at(0).code == "window-failed");
  CHECK(diagnosis.reasons[0].param("display") == "none");
}

TEST_CASE("little video memory on a dedicated card is a warning") {
  SystemFacts facts = on(OsKind::Windows);
  facts.devices = {rtx2060()};
  facts.devices[0].vramBytes = 512ULL * 1024ULL * 1024ULL;
  const Diagnosis diagnosis = diagnose(facts);
  CHECK(codes(diagnosis) == std::vector<std::string>{"low-vram"});
  CHECK(diagnosis.reasons[0].param("vram") == "512");
}

TEST_CASE("the package's software driver alone says nothing about the computer's GPU") {
  SystemFacts facts = on(OsKind::Windows);
  facts.devices = {lavapipe()};
  facts.adapters = {adapter("Intel(R) HD Graphics 4000", 0x8086)};
  facts.driversNarrowed = true;
  const Diagnosis diagnosis = diagnose(facts);
  CHECK_FALSE(diagnosis.chosen);
  CHECK(diagnosis.reasons.empty());
}

TEST_CASE("the JSON names every GPU and every reason") {
  SystemFacts facts = on(OsKind::Windows);
  facts.devices = {gpu("NVIDIA GeForce GTX 1060 6GB", 0x10de, GpuKind::Discrete, VK_API_VERSION_1_2,
                       VK_DRIVER_ID_NVIDIA_PROPRIETARY, nvidiaVersion(457, 30))};
  const std::string json = diagnosisJson(facts, diagnose(facts));
  CHECK(json.find(R"("name":"NVIDIA GeForce GTX 1060 6GB")") != std::string::npos);
  CHECK(json.find(R"("driverVersion":"457.30")") != std::string::npos);
  CHECK(json.find(R"("floor":"1.2")") != std::string::npos);
  CHECK(json.find(R"("code":"compatible-path")") != std::string::npos);
  CHECK(json.find(R"("chosen":0)") != std::string::npos);
  CHECK(json.find('\n') == std::string::npos);
}
