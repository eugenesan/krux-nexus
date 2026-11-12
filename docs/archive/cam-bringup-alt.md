# Nexus 5 (Hammerhead) Camera HAL Investigation Notes

## Purpose

This document summarizes discoveries, evidence, conclusions, and recommended next steps for a local coding/reverse-engineering agent tasked with producing a minimal camera capture proof-of-concept (POC) on Hammerhead.

Target objective:

```text
MicroPython
    ->
Capture helper
    ->
Qualcomm Camera HAL
    ->
JPEG/YUV frame
```

Long-term goal:

```text
Single-purpose camera capture support
without requiring full Android framework.
```

---

# Service Investigation Results

Observed services:

```text
service qcamerasvr
    /vendor/bin/mm-qcamera-daemon

service vendor.camera.provider
    /vendor/bin/hw/android.hardware.camera.provider-service.lineage
```

Properties confirmed:

```text
init.svc.qcamerasvr=running
init.svc.vendor.camera.provider=running
```

Process list:

```text
mm-qcamera-daemon
android.hardware.camera.provider-service.lineage
```

## Experiment

Provider stopped:

```bash
stop vendor.camera.provider
```

Results:

```text
mm-qcamera-daemon remains running
ICameraProvider disappears
dumpsys media.camera fails
```

Provider restarted:

```bash
start vendor.camera.provider
```

Results:

```text
android.hardware.camera.provider.ICameraProvider/internal/0 reappears
```

### Conclusion

`mm-qcamera-daemon` alone is not a usable camera API surface.

The provider is the component exposing camera functionality to userspace.

---

# Camera Provider Analysis

Binary:

```text
/vendor/bin/hw/android.hardware.camera.provider-service.lineage
```

Dependencies:

```text
camera.device-impl.lineage.so
libbinder_ndk.so
libhardware.so
libcamera_metadata.so
```

Evidence from exported strings:

```text
camera_module->open
camera_module->get_number_of_cameras
camera_module->get_camera_info
```

### Conclusion

The provider loads a traditional Android camera HAL module.

It is not talking directly to Qualcomm hardware.

Likely flow:

```text
CameraProvider
    ->
Camera HAL module
    ->
Qualcomm camera stack
```

---

# camera.device-impl.lineage.so Analysis

This library appears to be a thin adapter between:

```text
AIDL Camera Provider
```

and

```text
camera3_device_t
```

Key symbols:

```text
CameraDeviceSession::initialize
CameraDeviceSession::configureStreams
CameraDeviceSession::processCaptureRequest
CameraDeviceSession::importBuffer
CameraDeviceSession::importRequestImpl
CameraDeviceSession::sProcessCaptureResult
```

### Interpretation

This library largely performs:

```text
AIDL conversion
buffer import
request conversion
result conversion
```

rather than camera control itself.

---

# Hammerhead Camera HAL Findings

HAL path:

```text
/system/vendor/lib/hw/camera.hammerhead.so
```

Strong evidence of full Camera HAL3 implementation:

```text
QCamera3HardwareInterface
QCamera3Factory
QCamera3Channel
QCamera3Stream
QCamera3PicChannel
QCamera3RawChannel
QCamera3GrallocMemory
```

Camera3 operations discovered:

```text
initialize
configure_streams
process_capture_request
construct_default_request_settings
flush
close
```

### Conclusion

This is a genuine Qualcomm Camera HAL3 implementation.

Not HAL1.
Not a shim.
Not a wrapper over Camera2.

The HAL itself already implements:

```text
stream management
capture requests
JPEG processing
callbacks
metadata handling
```

---

# Important Architecture Discovery

Most likely stack:

```text
camera.device-impl.lineage.so
        |
        v
camera.hammerhead.so
        |
        v
libmmcamera_interface.so
        |
        v
mm-qcamera-daemon
        |
        v
kernel camera drivers
```

This means:

```text
Provider
```

is mostly glue.

Actual camera functionality resides below it.

---

# Buffer Handling Investigation

Critical findings:

```text
GraphicBufferMapper::lockYCbCr
GraphicBufferMapper::lock
GraphicBufferMapper::unlock
GraphicBufferMapper::importBufferNoValidate
```

and:

```text
HandleImporter::importBuffer
HandleImporter::freeBuffer
```

### Interpretation

The Lineage adapter expects:

```text
native_handle_t*
```

backed buffers.

It imports and maps those buffers.

It does NOT appear to allocate them.

---

# Revised Understanding of the Problem

Originally suspected:

```text
need provider
need binder
need camera framework
```

Current assessment:

```text
Camera HAL already works
Camera HAL already captures
Camera HAL already contains JPEG support
```

The hardest remaining problem is:

```text
allocating a valid Camera3 target buffer
```

not camera access itself.

---

# Current Hypothesis

A direct HAL3 capture utility should be feasible.

Desired architecture:

```text
capture-helper
        |
        v
camera.hammerhead.so
        |
        v
mm-qcamera-daemon
```

Avoid:

```text
libhybris
CameraService
Android framework
```

unless later evidence shows they are required.

---

# Recommended POC Stages

## Stage 1

Load HAL.

```cpp
hw_get_module()
```

Verify:

```cpp
get_number_of_cameras()
get_camera_info()
```

Expected result:

Enumeration matches Android.

---

## Stage 2

Open camera.

```cpp
camera_module->open()
```

Obtain:

```cpp
camera3_device_t*
```

Run:

```cpp
initialize()
```

---

## Stage 3

Configure single stream.

Recommended:

```text
640x480
HAL_PIXEL_FORMAT_YCbCr_420_888
```

Reason:

Widely supported.
Easy CPU access.
Supported by Android stack.

---

## Stage 4

Solve buffer allocation.

Primary research target:

```text
libui.so
GraphicBuffer
GraphicBufferAllocator
AHardwareBuffer
```

Need valid:

```cpp
native_handle_t*
```

for Camera3 stream buffers.

---

## Stage 5

Submit request.

```cpp
process_capture_request()
```

Receive callback:

```cpp
process_capture_result()
```

---

## Stage 6

Map returned buffer.

Likely using:

```cpp
GraphicBufferMapper::lockYCbCr()
```

Extract:

```text
Y plane
Cb plane
Cr plane
```

Save:

```text
frame.yuv
```

or convert to JPEG.

---

# Recommended Immediate Recon Work

1. Inspect libui.so exports.
2. Search for GraphicBufferAllocator.
3. Search for AHardwareBuffer allocator APIs.
4. Determine how Android camera clients allocate stream buffers.
5. Build smallest possible HAL3 harness.
6. Validate HAL open before implementing capture.

---

# Confidence Assessment

High confidence:

- Qualcomm HAL3 implementation exists.
- Provider uses standard Camera3 APIs.
- Camera capture path is functional on Android 16.
- Direct HAL approach is technically viable.

Medium confidence:

- Provider and Binder can be completely bypassed.
- Only mm-qcamera-daemon is needed at runtime.

Largest remaining unknown:

```text
Reliable allocation of Camera3-compatible native_handle buffers.
```

Current estimate:

```text
10% camera control
20% stream configuration
70% buffer allocation/integration
```

This should be the primary focus of subsequent investigation.
