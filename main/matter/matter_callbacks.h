#pragma once

#include <stdint.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app/ConcreteAttributePath.h>
#include <app/AttributePathParams.h>
#include <app/EventPathParams.h>
#include <lib/core/CHIPError.h>
#include <lib/core/TLV.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void OnAttributeData(uint64_t node_id,
                         const chip::app::ConcreteDataAttributePath &path,
                         chip::TLV::TLVReader *data);
    void OnReadDone(
        uint64_t node_id,
        const chip::Platform::ScopedMemoryBufferWithSize<chip::app::AttributePathParams> &attr_paths,
        const chip::Platform::ScopedMemoryBufferWithSize<chip::app::EventPathParams> &event_paths);

#ifdef __cplusplus
}
#endif