// Copyright (C) 2023 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "register.hpp"
#include "reorder_inst.h"
#include "implementation_map.hpp"

#include "intel_gpu/runtime/error_handler.hpp"

#include "openvino/op/convert.hpp"

namespace cldnn {
namespace cpu {

struct reorder_impl : public typed_primitive_impl<reorder> {
    using parent = typed_primitive_impl<reorder>;
    using parent::parent;

    std::shared_ptr<ov::op::v0::Convert> op;

    DECLARE_OBJECT_TYPE_SERIALIZATION

    std::unique_ptr<primitive_impl> clone() const override {
        return make_unique<reorder_impl>(*this);
    }

    reorder_impl() : parent("reorder_cpu_impl") {}

    explicit reorder_impl(const reorder_node& outer) {
        set_node_params(outer);
    }

    void set_node_params(const program_node& arg) override {
        OPENVINO_ASSERT(arg.is_type<reorder>(), "[GPU] Incorrect program_node type");
    }

    event::ptr execute_impl(const std::vector<event::ptr>& events, reorder_inst& instance) override {
        OV_ITT_SCOPED_TASK(ov::intel_gpu::itt::domains::intel_gpu_plugin, "reorder::execute_impl");
        auto& stream = instance.get_network().get_stream();

        for (auto e : events) {
            e->wait();
        }

        auto ev = stream.create_user_event(false);

        if (instance.get_impl_params()->input_layouts[0].format != instance.get_impl_params()->input_layouts[0].format)
            OPENVINO_THROW("[GPU] Unsupported reorder case: input and output type are different");

        ov::TensorVector input_host_tensors;
        ov::TensorVector output_host_tensors;

        auto input_mem_ptr = instance.input_memory_ptr();
        auto output_mem_ptr = instance.output_memory_ptr();

        cldnn::mem_lock<uint8_t, mem_lock_type::read> input_lock(input_mem_ptr, stream);
        cldnn::mem_lock<uint8_t, mem_lock_type::read> output_lock(output_mem_ptr, stream);

        input_host_tensors.push_back(make_tensor(input_mem_ptr->get_layout(), input_lock.data()));
        output_host_tensors.push_back(make_tensor(output_mem_ptr->get_layout(), output_lock.data()));

        if (!op) {
            op = std::make_shared<ov::op::v0::Convert>();
        }

        OPENVINO_ASSERT(op->evaluate(output_host_tensors, input_host_tensors),
                        "[GPU] Couldn't execute reorder primitive with id ", instance.id());

        ev->set();

        return ev;
    }

    void init_kernels(const kernels_cache& , const kernel_impl_params&) override {}

    void update_dispatch_data(const kernel_impl_params& impl_param) override {}

public:
    static std::unique_ptr<primitive_impl> create(const reorder_node& arg, const kernel_impl_params& impl_param) {
        return make_unique<reorder_impl>();
    }
};


namespace detail {

attach_reorder_impl::attach_reorder_impl() {
    auto formats = {
        format::bfyx,
        format::bfzyx,
        format::bfwzyx,
        format::bfuwzyx,
        format::bfvuwzyx
    };

    auto types = {
        data_types::f32,
        data_types::f16,
        data_types::i32,
        data_types::i64,
        data_types::i8,
        data_types::u8,
    };

    implementation_map<reorder>::add(impl_types::cpu, shape_types::static_shape, reorder_impl::create, types, formats);
    implementation_map<reorder>::add(impl_types::cpu, shape_types::dynamic_shape, reorder_impl::create, types, formats);
}

}  // namespace detail
}  // namespace cpu
}  // namespace cldnn

BIND_BINARY_BUFFER_WITH_TYPE(cldnn::cpu::reorder_impl)
