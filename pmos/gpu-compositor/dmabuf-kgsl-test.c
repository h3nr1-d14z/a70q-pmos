// Feasibility test for a GPU-rendered compositor on a70q (downstream 4.14):
// can turnip-kgsl (Adreno 612 via /dev/kgsl-3d0) render into a buffer that belongs to the
// display driver (SDE msm_drm)?  1) allocate a GEM buffer on the SDE DRM device and export
// it as dma-buf, 2) import it into Vulkan (VK_EXT_external_memory_dma_buf), 3) let the GPU
// fill a VkBuffer and clear a linear VkImage bound to it, 4) check the bytes from the CPU.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <libdrm/msm_drm.h>
#include <vulkan/vulkan.h>

#define W 256
#define H 256
#define CHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { printf("FAIL %s = %d\n", #x, r_); exit(1); } } while (0)

static int alloc_gem(int fd, size_t size, uint32_t *handle) {
	struct drm_msm_gem_new req = { .size = size, .flags = MSM_BO_WC };
	if (drmIoctl(fd, DRM_IOCTL_MSM_GEM_NEW, &req)) { perror("MSM_GEM_NEW"); return -1; }
	*handle = req.handle;
	int dfd;
	if (drmPrimeHandleToFD(fd, req.handle, DRM_CLOEXEC | DRM_RDWR, &dfd)) { perror("PrimeHandleToFD"); return -1; }
	return dfd;
}

int main(int argc, char **argv) {
	const char *node = argc > 1 ? argv[1] : "/dev/dri/renderD128";
	int fd = open(node, O_RDWR | O_CLOEXEC);
	if (fd < 0) { perror(node); return 1; }
	drmVersionPtr v = drmGetVersion(fd);
	printf("drm: %s (%s)\n", v->name, v->desc);
	size_t size = W * H * 4;
	uint32_t gem;
	int dmabuf = alloc_gem(fd, size, &gem);
	if (dmabuf < 0) return 1;
	uint8_t *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, dmabuf, 0);
	if (map == MAP_FAILED) { perror("mmap dmabuf"); return 1; }
	memset(map, 0x11, size);
	printf("gem handle %u -> dma-buf fd %d, CPU mmap ok\n", gem, dmabuf);

	VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1 };
	VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
	VkInstance inst; CHECK(vkCreateInstance(&ici, NULL, &inst));
	uint32_t n = 1; VkPhysicalDevice pd; vkEnumeratePhysicalDevices(inst, &n, &pd);
	if (!n) { printf("no vulkan device\n"); return 1; }
	VkPhysicalDeviceProperties pp; vkGetPhysicalDeviceProperties(pd, &pp);
	printf("vk: %s api %u.%u\n", pp.deviceName, VK_VERSION_MAJOR(pp.apiVersion), VK_VERSION_MINOR(pp.apiVersion));
	uint32_t ne = 0; vkEnumerateDeviceExtensionProperties(pd, NULL, &ne, NULL);
	VkExtensionProperties *ep = calloc(ne, sizeof(*ep)); vkEnumerateDeviceExtensionProperties(pd, NULL, &ne, ep);
	const char *want[] = { "VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf",
		"VK_EXT_image_drm_format_modifier", "VK_EXT_queue_family_foreign", "VK_KHR_external_semaphore_fd",
		"VK_EXT_physical_device_drm", "VK_KHR_timeline_semaphore", "VK_KHR_synchronization2" };
	int have[8] = {0};
	for (int i = 0; i < 8; i++) { for (uint32_t j = 0; j < ne; j++) if (!strcmp(ep[j].extensionName, want[i])) have[i] = 1;
		printf("  %-34s %s\n", want[i], have[i] ? "yes" : "NO"); }
	if (!have[0] || !have[1]) return 1;

	float prio = 1;
	VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount = 1, .pQueuePriorities = &prio };
	const char *dext[] = { "VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf" };
	VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
		.enabledExtensionCount = 2, .ppEnabledExtensionNames = dext };
	VkDevice dev; CHECK(vkCreateDevice(pd, &dci, NULL, &dev));
	VkQueue q; vkGetDeviceQueue(dev, 0, 0, &q);
	PFN_vkGetMemoryFdPropertiesKHR getFdProps = (void *)vkGetDeviceProcAddr(dev, "vkGetMemoryFdPropertiesKHR");
	VkMemoryFdPropertiesKHR fdp = { .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR };
	CHECK(getFdProps(dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, dmabuf, &fdp));
	printf("dma-buf memoryTypeBits 0x%x\n", fdp.memoryTypeBits);

	// linear RGBA image on the imported memory
	VkExternalMemoryImageCreateInfo emi = { .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
		.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
	VkImageCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .pNext = &emi, .imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_B8G8R8A8_UNORM, .extent = { W, H, 1 }, .mipLevels = 1, .arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_LINEAR,
		.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
	VkImage img; CHECK(vkCreateImage(dev, &ii, NULL, &img));
	VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, img, &mr);
	VkSubresourceLayout sl; VkImageSubresource isr = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
	vkGetImageSubresourceLayout(dev, img, &isr, &sl);
	printf("image: size %llu rowPitch %llu (dumb pitch would be %d)\n", (unsigned long long)mr.size, (unsigned long long)sl.rowPitch, W * 4);
	uint32_t mt = 0; uint32_t bits = mr.memoryTypeBits & fdp.memoryTypeBits; while (bits && !(bits & 1)) { bits >>= 1; mt++; }
	VkImportMemoryFdInfoKHR imp = { .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
		.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, .fd = dup(dmabuf) };
	VkMemoryDedicatedAllocateInfo ded = { .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .pNext = &imp, .image = img };
	VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &ded, .allocationSize = size > mr.size ? size : mr.size, .memoryTypeIndex = mt };
	VkDeviceMemory mem; CHECK(vkAllocateMemory(dev, &mai, NULL, &mem));
	CHECK(vkBindImageMemory(dev, img, mem, 0));
	printf("imported dma-buf into VkDeviceMemory + bound linear image\n");

	VkCommandPoolCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	VkCommandPool pool; CHECK(vkCreateCommandPool(dev, &cpi, NULL, &pool));
	VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .commandBufferCount = 1 };
	VkCommandBuffer cb; CHECK(vkAllocateCommandBuffers(dev, &cai, &cb));
	VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	CHECK(vkBeginCommandBuffer(cb, &bi));
	VkImageSubresourceRange rng = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	VkImageMemoryBarrier b1 = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .image = img, .subresourceRange = rng,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED };
	vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b1);
	VkClearColorValue col = { .float32 = { 1.0f, 0.0f, 0.0f, 1.0f } }; // R=1 -> BGRA bytes 00 00 ff ff
	vkCmdClearColorImage(cb, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &col, 1, &rng);
	VkImageMemoryBarrier b2 = b1; b2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b2.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	b2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b2.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 0, NULL, 1, &b2);
	CHECK(vkEndCommandBuffer(cb));
	VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
	VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO }; VkFence fence; CHECK(vkCreateFence(dev, &fci, NULL, &fence));
	CHECK(vkQueueSubmit(q, 1, &si, fence));
	CHECK(vkWaitForFences(dev, 1, &fence, VK_TRUE, 2000000000ull));
	int ok = 0, bad = 0;
	for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
		uint8_t *p = map + y * sl.rowPitch + x * 4;
		if (p[0] == 0x00 && p[1] == 0x00 && p[2] == 0xff && p[3] == 0xff) ok++; else bad++;
	}
	printf("GPU clear seen by CPU through the SDE GEM mapping: %d/%d pixels correct (first: %02x %02x %02x %02x)\n",
		ok, ok + bad, map[0], map[1], map[2], map[3]);
	return bad ? 2 : 0;
}
