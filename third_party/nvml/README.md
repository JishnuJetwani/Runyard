# NVIDIA Management Library header

`nvml.h` is retained verbatim from NVIDIA/go-nvml v0.12.4-1, commit
`8a7f6b796317a8c7cd5347a25be164108967e7b4`, `pkg/nvml/nvml.h`.
It includes the upstream NVIDIA license notice.
Source: https://github.com/NVIDIA/go-nvml/tree/8a7f6b796317a8c7cd5347a25be164108967e7b4

Runyard loads the driver's library dynamically. This header is private to the
NVIDIA adapter and its ABI fixture; runtime programs do not link against NVML.
