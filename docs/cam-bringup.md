# Hammerhead Camera Bringup Failure Analysis and Recovery Plan

# Purpose

This document focuses specifically on the earlier reverse-engineering effort that successfully reached camera enumeration but failed to obtain image pixels.

The goal is to help a coding agent continue that original work rather than starting over.

---

# Executive Summary

Based on the artifacts examined so far, it is increasingly unlikely that the failure was caused by:

- sensor access
- Qualcomm ISP initialization
- mm-qcamera-daemon startup
- camera enumeration
- camera metadata retrieval
- camera HAL loading

Evidence strongly suggests the failure occurred after successful HAL bringup and during one of the following stages:

```text
buffer allocation
buffer registration
stream configuration
capture request construction
capture result handling
fence synchronization
buffer mapping
```

These are the hardest parts of Camera HAL3 integration.

---

# Evidence Collected

## Camera HAL3 implementation exists

camera.hammerhead.so exports:

```text
configure_streams
process_capture_request
construct_default_request_settings
initialize
flush
```

This is a full Camera HAL3 implementation.

Therefore camera acquisition is request-driven using HAL3.

---

## Camera provider works

Android successfully exposes:

```text
android.hardware.camera.provider.ICameraProvider/internal/0
```

and camera metadata was previously dumped.

This proves:

```text
HAL loaded successfully
cameras detected
metadata generated
camera stack operational
```

---

## Provider adapter uses standard Camera3 APIs

camera.device-impl.lineage.so exports:

```text
CameraDeviceSession::configureStreams
CameraDeviceSession::processCaptureRequest
CameraDeviceSession::sProcessCaptureResult
```

which are direct Camera3 concepts.

---

# Most Likely Failure Modes

# Failure Mode 1 - No Valid Stream Buffer

Probability: Very High

HAL3 does not allocate output buffers.

Client must provide them.

Expected flow:

```text
allocate buffer
register buffer
configure stream
submit capture request
receive filled buffer
```

If request references an invalid or incompatible buffer:

```text
process_capture_request()
returns failure
```

or:

```text
request accepted
no usable image returned
```

Common symptoms:

```text
camera opens
metadata works
capture never completes
```

---

# Failure Mode 2 - Incorrect Gralloc Handle

Probability: Very High

QCamera3 symbols indicate use of:

```text
QCamera3GrallocMemory
native_handle
registerBuffer
```

Many RE attempts fabricate buffer structures.

HAL expects:

```text
real gralloc allocation
```

not:

```text
malloc()
fake native_handle
```

A fake handle may allow:

```text
configure_streams()
```

but fail later.

---

# Failure Mode 3 - Stream Configuration Rejected

Probability: High

Camera3 requires coherent stream definitions.

Examples:

```text
supported format
supported dimensions
usage flags
rotation
```

Incorrect stream configuration often results in:

```text
configure_streams failure
```

or:

```text
capture request failure
```

Recommended first target:

```text
640x480
YCbCr_420_888
```

Use simplest supported format.

---

# Failure Mode 4 - Missing Callback Plumbing

Probability: High

Camera3 is callback-based.

HAL uses:

```text
process_capture_result()
notify()
```

Application must implement:

```cpp
camera3_callback_ops
```

If callbacks are incorrect:

```text
capture may succeed internally
results never propagated
```

Symptoms:

```text
request submitted
nothing returned
```

---

# Failure Mode 5 - Fence Handling

Probability: Medium-High

Modern Android graphics heavily uses sync fences.

Results may contain:

```text
release_fence
acquire_fence
```

If fence synchronization is skipped:

```text
buffer read before completion
```

leading to:

```text
empty image
corrupted image
stale image
```

Many reverse-engineering projects get stuck here.

---

# Failure Mode 6 - Buffer Mapping Error

Probability: Medium

Even if capture succeeds:

```text
buffer must be mapped correctly
```

Evidence from provider:

```text
GraphicBufferMapper::lockYCbCr
GraphicBufferMapper::unlock
```

If mapping is wrong:

```text
successful capture
unreadable output
```

Symptoms:

```text
all black image
invalid dimensions
crashes
```

---

# Failure Mode 7 - Request Metadata

Probability: Medium

HAL3 requires request metadata.

Common path:

```cpp
construct_default_request_settings()
```

then modify.

Using incomplete metadata may result in:

```text
request accepted
capture never starts
```

---

# Failure Mode 8 - Stream Buffer Registration Lifecycle

Probability: Medium

Camera3 expects consistency:

```text
configure stream
register buffers
submit requests
```

Violating lifecycle ordering can lead to:

```text
silent request failures
```

especially in older Qualcomm HALs.

---

# Why Sensor Access Is Probably Not The Problem

Evidence:

```text
CameraProvider operational
Camera metadata available
Camera IDs discovered
```

To reach that point:

```text
sensor query succeeded
HAL initialization succeeded
camera module loaded
```

Therefore the stack has already crossed most low-level hardware barriers.

---

# Why mm-qcamera-daemon Is Probably Not The Problem

The HAL exports numerous QCamera3 classes:

```text
QCamera3Stream
QCamera3Channel
QCamera3HardwareInterface
```

These rely on:

```text
libmmcamera_interface
mm-qcamera-daemon
```

If daemon communication was fundamentally broken:

```text
camera enumeration likely fails
```

much earlier.

---

# Recommended Recovery Strategy

## Phase 1 - Reconstruct Minimal Successful Open

Goal:

```text
load HAL
enumerate cameras
open camera
initialize camera3_device
```

No streaming.

No capture.

Just verify startup.

---

## Phase 2 - Verify Stream Configuration

Goal:

```text
configure_streams succeeds
```

Use:

```text
640x480
YCbCr_420_888
single output stream
```

Log every structure field.

---

## Phase 3 - Solve Allocation Separately

Do NOT combine with capture.

Create standalone test:

```text
allocate graphic buffer
import buffer
lock buffer
unlock buffer
free buffer
```

Only after allocation works:

```text
connect camera
```

---

## Phase 4 - Instrument Every Callback

Log:

```text
notify
process_capture_result
partial result
shutter event
error event
```

Every callback should be timestamped.

---

## Phase 5 - Single Capture Request

Only one request.

No preview loop.

No repeating requests.

Desired flow:

```text
open
configure
capture once
save image
exit
```

---

# Recommended Reverse Engineering Targets

Priority 1:

```text
CameraDeviceSession::importBuffer
HandleImporter::importBuffer
```

Understand expected native_handle lifecycle.

Priority 2:

```text
CameraDeviceSession::configureStreams
```

Understand stream structure expectations.

Priority 3:

```text
CameraDeviceSession::processOneCaptureRequest
```

Understand request construction.

Priority 4:

```text
CameraDeviceSession::sProcessCaptureResult
```

Understand result delivery.

---

# Practical Coding Recommendation

Do NOT attempt:

```text
continuous preview
JPEG encoding
multiple streams
reprocessing
```

for first milestone.

Instead:

```text
single YUV frame
save raw buffer
terminate
```

The shortest path to proving success is:

```text
one valid pixel
```

not a polished camera application.

---

# Final Assessment

Current evidence suggests the previous effort likely failed after successful camera bringup and before successful buffer acquisition.

The strongest working hypothesis is:

```text
invalid/incomplete Camera3 buffer pipeline
```

rather than:

```text
camera hardware failure
HAL loading failure
Qualcomm daemon failure
```

The investigation should therefore prioritize:

```text
buffer allocation
buffer registration
fence handling
capture-result processing
```

before spending additional effort on HAL loading or provider architecture.
