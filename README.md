# GPU occlusion culling using compute shader with Vulkan

<img src="https://i.imgur.com/ahdyeU5.png" width="400">

This demo follows the hierarchical depth comparing method described in [Experiments in GPU-Based Occlusion Culling](https://interplayoflight.wordpress.com/2017/11/15/experiments-in-gpu-based-occlusion-culling/) by Kostas Anagnostou. Mesh, instance, and indirect draw command data stay in device local memory after initialization.

The commands(`VkDrawIndexedIndirectCommand`) for `vkCmdDrawIndexedIndirect` in this demo is not batched into one command for all instances, or packed as in the article above, but expanded as per instance. As a result, culling can be performed by setting the `instanceCount` to zero or one of a command according to visibility computation. According to query data, the demo shows that per-instance commands do not slow down draw performance. Shown in the following screenshots of non-extreme cases, significant amount of draw time is saved by enabling frustum and occlusion culling. (The program also provides a frustum culling only mode for isolating the effects.)

The frame time taken by compute pipelines for mipchain generation and depth image transfer commands is not trivial. But the convenience remains that it does not raise with scene complexity.

#### case 1

<img src="https://i.imgur.com/YSmbDNa.png" width="600">

<img src="https://i.imgur.com/IwRm9XU.png" width="600">

<img src="https://i.imgur.com/MzbvGX2.png" width="600">

#### case 2

<img src="https://i.imgur.com/HqjW3nI.png" width="600">

<img src="https://i.imgur.com/eOD39kW.png" width="600">

<img src="https://i.imgur.com/bjuMlY9.png" width="600">

---

Controls:

- pan: ADRF
- forward/backward: WS
- orbit: arrow keys
- zoom: mousewheel
- F1: MDI batched no culling
- F2: MDI per-instance frustum culling
- F3: MDI per-instance frustum and occlusion culling
- F4: F3 with blending enabled
