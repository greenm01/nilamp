// SPDX-License-Identifier: MIT

#include "nilamp_topologies.h"

const NilampTopologyOps *nilamp_topology_ops(NilampTopologyId topology)
{
    switch (topology) {
    case NILAMP_TOPOLOGY_TWEED_5E3_CATHODYNE_PP:
        return &NILAMP_TWEED_5E3_PP_OPS;
    }
    return NULL;
}
