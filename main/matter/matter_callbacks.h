#pragma once

#include <stdint.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app/ConcreteAttributePath.h>
#include <lib/core/CHIPError.h>
#include <lib/core/TLV.h>

#ifdef __cplusplus
extern "C" {
#endif

void OnAttributeData(uint64_t node_id, 
                    const chip::app::ConcreteDataAttributePath &path,
                    chip::TLV::TLVReader *data);

#ifdef __cplusplus
}
#endif