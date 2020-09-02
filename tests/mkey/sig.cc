/**
 * Copyright (C) 2020      Mellanox Technologies Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define __STDC_LIMIT_MACROS
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include "env.h"
#include "mkey.h"

#define DATA_SIZE 4096

template<typename SrcSigBlock, uint64_t SrcValue,
	 typename DstSigBlock, uint64_t DstValue,
	 uint32_t NumBlocks = 1,
	 typename RdmaOp = rdma_op_read<ibvt_qp_dv>>
struct _mkey_test_sig_block : public mkey_test_base<ibvt_qp_dv> {
	static constexpr uint32_t src_block_size = SrcSigBlock::MkeyDomainType::BlockSizeType::block_size;
	static constexpr uint32_t src_sig_size = SrcSigBlock::MkeyDomainType::SigType::sig_size;
	static constexpr uint32_t src_data_size = NumBlocks * (src_block_size + src_sig_size);
	static constexpr uint32_t dst_block_size = DstSigBlock::MkeyDomainType::BlockSizeType::block_size;
	static constexpr uint32_t dst_sig_size = DstSigBlock::MkeyDomainType::SigType::sig_size;
	static constexpr uint32_t dst_data_size = NumBlocks * (dst_block_size + dst_sig_size);

	struct mkey_dv_new<mkey_basic_attr<>,
			   mkey_layout_new_list_mrs<src_data_size>,
			   SrcSigBlock> src_mkey;
	struct mkey_dv_new<mkey_basic_attr<>,
			   mkey_layout_new_list_mrs<dst_data_size>,
			   DstSigBlock> dst_mkey;
	RdmaOp rdma_op;

	_mkey_test_sig_block() :
		src_mkey(*this, this->src_side.pd, 1, MLX5DV_MKEY_INIT_ATTR_FLAGS_INDIRECT |
			 MLX5DV_MKEY_INIT_ATTR_FLAGS_BLOCK_SIGNATURE),
		dst_mkey(*this, this->dst_side.pd, 1, MLX5DV_MKEY_INIT_ATTR_FLAGS_INDIRECT |
			 MLX5DV_MKEY_INIT_ATTR_FLAGS_BLOCK_SIGNATURE) {}

	virtual void SetUp() override {
		mkey_test_base<ibvt_qp_dv>::SetUp();
		EXEC(src_mkey.init());
		EXEC(dst_mkey.init());
	}

	bool is_supported() {
		struct mlx5dv_context attr = {};
		attr.comp_mask = MLX5DV_CONTEXT_MASK_SIGNATURE_OFFLOAD;
		mlx5dv_query_device(this->ctx.ctx, &attr);
		return SrcSigBlock::is_supported(attr) && DstSigBlock::is_supported(attr);
	}

	void fill_data() {
		uint8_t src_buf[src_data_size];
		uint8_t *buf = src_buf;
		uint64_t value = SrcValue;

		memset(src_buf, 0xA5, src_data_size);
		for (uint32_t i = 0; i < NumBlocks; ++i) {
			buf += src_block_size;
			SrcSigBlock::MkeyDomainType::SigType::sig_to_buf(value, buf);
			buf += src_sig_size;
		}
		src_mkey.layout->set_data(src_buf, src_data_size);
	}

	void check_data() {
		uint8_t dst_buf[dst_data_size];
		uint8_t *buf = dst_buf;
		uint8_t ref_block_buf[dst_block_size];
		uint64_t value = DstValue;
		uint8_t ref_sig_buf[dst_sig_size];

		VERBS_TRACE("SrcBlockSize %u, SrcSigSize %u, DstBlockSize %u, DstSigSize %u\n",
			    src_block_size, src_sig_size, dst_block_size, dst_sig_size);
		memset(ref_block_buf, 0xA5, dst_block_size);
		SrcSigBlock::MkeyDomainType::SigType::sig_to_buf(value, ref_sig_buf);
		dst_mkey.layout->get_data(dst_buf, dst_data_size);
		for (uint32_t i = 0; i < NumBlocks; ++i) {
			ASSERT_EQ(0, memcmp(buf, ref_block_buf, dst_block_size));
			buf += dst_block_size;
			ASSERT_EQ(0, memcmp(buf, ref_sig_buf, dst_sig_size));
			buf += dst_sig_size;
		}
	}

	void configure_mkeys() {
		auto &src_side = this->src_side;
		auto &dst_side = this->dst_side;
		dst_side.qp.wr_flags(IBV_SEND_SIGNALED | IBV_SEND_INLINE);
		EXEC(dst_mkey.configure(dst_side.qp));
		EXEC(dst_side.cq.poll());

		src_side.qp.wr_flags(IBV_SEND_SIGNALED | IBV_SEND_INLINE);
		EXEC(src_mkey.configure(src_side.qp));
		EXEC(src_side.cq.poll());
	}

	void check_mkeys() {
		EXEC(src_mkey.check());
		EXEC(dst_mkey.check());
	}

	void execute_rdma() {
		auto &src_side = this->src_side;
		auto &dst_side = this->dst_side;
		EXEC(rdma_op.submit(src_side, src_mkey.sge(),
				    dst_side, dst_mkey.sge()));
		EXEC(rdma_op.complete(src_side, dst_side));
	}
};


template<typename T_SrcSigBlock, uint64_t T_SrcValue,
	 typename T_DstSigBlock, uint64_t T_DstValue,
	 uint32_t T_NumBlocks = 1,
	 typename T_RdmaOp = rdma_op_read<ibvt_qp_dv>>
struct types {
	typedef T_SrcSigBlock SrcSigBlock;
	static constexpr uint64_t SrcValue = T_SrcValue;
	typedef T_DstSigBlock DstSigBlock;
	static constexpr uint64_t DstValue = T_DstValue;
	static constexpr uint64_t NumBlocks = T_NumBlocks;
	typedef T_RdmaOp RdmaOp;
};

template<typename T>
using mkey_test_sig_block = _mkey_test_sig_block<typename T::SrcSigBlock, T::SrcValue,
						 typename T::DstSigBlock, T::DstValue,
						 T::NumBlocks,
						 typename T::RdmaOp>;

TYPED_TEST_CASE_P(mkey_test_sig_block);

#define SIG_CHK_SUT() \
	CHK_SUT(dv_sig); \
	if (!this->is_supported()) SKIP(1);

TYPED_TEST_P(mkey_test_sig_block, basic) {
	SIG_CHK_SUT();

	EXEC(fill_data());
	EXEC(configure_mkeys());
	EXEC(execute_rdma());
	// this->src_mkey.layout->dump(0, 0, "SRC");
	// this->dst_mkey.layout->dump(0, 0, "DST");
	EXEC(check_mkeys());
	EXEC(check_data());
}

REGISTER_TYPED_TEST_CASE_P(mkey_test_sig_block, basic);

typedef testing::Types<
	types<mkey_sig_block_none, 0,
	      mkey_sig_block_none, 0>,

	// Wire domain
	types<mkey_sig_block<mkey_sig_block_domain_none,
			     mkey_sig_block_domain<mkey_sig_crc32ieee, mkey_sig_block_size_512>>, 0,
	      mkey_sig_block<mkey_sig_block_domain_none,
			     mkey_sig_block_domain<mkey_sig_crc32ieee, mkey_sig_block_size_512>>, 0>,
	types<mkey_sig_block<mkey_sig_block_domain_none,
			     mkey_sig_block_domain<mkey_sig_crc32c, mkey_sig_block_size_512>>, 0,
	      mkey_sig_block<mkey_sig_block_domain_none,
			     mkey_sig_block_domain<mkey_sig_crc32c, mkey_sig_block_size_512>>, 0>,
	types<mkey_sig_block<mkey_sig_block_domain_none,
			     mkey_sig_block_domain<mkey_sig_crc64xp10, mkey_sig_block_size_512>>, 0,
	      mkey_sig_block<mkey_sig_block_domain_none,
			     mkey_sig_block_domain<mkey_sig_crc64xp10, mkey_sig_block_size_512>>, 0>,

	// Mkey domain
	types<mkey_sig_block<mkey_sig_block_domain<mkey_sig_crc32ieee, mkey_sig_block_size_512>,
			     mkey_sig_block_domain_none>, 0x699ACA21,
	      mkey_sig_block<mkey_sig_block_domain<mkey_sig_crc32c, mkey_sig_block_size_512>,
			     mkey_sig_block_domain_none>, 0x7BE5157D>,
	// @todo: check crc64 signature
	types<mkey_sig_block<mkey_sig_block_domain<mkey_sig_crc32ieee, mkey_sig_block_size_512>,
			     mkey_sig_block_domain_none>, 0x699ACA21,
	      mkey_sig_block<mkey_sig_block_domain<mkey_sig_crc64xp10, mkey_sig_block_size_512>,
			     mkey_sig_block_domain_none>, 0xB23C348A1F86783F>
	> mkey_test_list_sig_types;
INSTANTIATE_TYPED_TEST_CASE_P(sig_types, mkey_test_sig_block, mkey_test_list_sig_types);

typedef testing::Types<
	types<mkey_sig_block<mkey_sig_block_domain<mkey_sig_crc32ieee, mkey_sig_block_size_512>,
			     mkey_sig_block_domain<mkey_sig_crc32ieee, mkey_sig_block_size_512>>, 0x699ACA21,
	      mkey_sig_block<mkey_sig_block_domain<mkey_sig_crc32ieee, mkey_sig_block_size_512>,
			     mkey_sig_block_domain<mkey_sig_crc32ieee, mkey_sig_block_size_512>>, 0x699ACA21,
	      1, rdma_op_read<ibvt_qp_dv>>,
	types<mkey_sig_block<mkey_sig_block_domain<mkey_sig_crc32ieee, mkey_sig_block_size_512>,
			     mkey_sig_block_domain<mkey_sig_crc32ieee, mkey_sig_block_size_512>>, 0x699ACA21,
	      mkey_sig_block<mkey_sig_block_domain<mkey_sig_crc32ieee, mkey_sig_block_size_512>,
			     mkey_sig_block_domain<mkey_sig_crc32ieee, mkey_sig_block_size_512>>, 0x699ACA21,
	      1, rdma_op_write<ibvt_qp_dv>>,
	types<mkey_sig_block<mkey_sig_block_domain<mkey_sig_crc32ieee, mkey_sig_block_size_512>,
			     mkey_sig_block_domain<mkey_sig_crc32ieee, mkey_sig_block_size_512>>, 0x699ACA21,
	      mkey_sig_block<mkey_sig_block_domain<mkey_sig_crc32ieee, mkey_sig_block_size_512>,
			     mkey_sig_block_domain<mkey_sig_crc32ieee, mkey_sig_block_size_512>>, 0x699ACA21,
	      1, rdma_op_send<ibvt_qp_dv>>

	> mkey_test_list_ops;
INSTANTIATE_TYPED_TEST_CASE_P(ops, mkey_test_sig_block, mkey_test_list_ops);


typedef mkey_test_base<ibvt_qp_dv> mkey_test_sig_custom;

TEST_F(mkey_test_sig_custom, noBlockSigAttr) {
	// @todo: add caps check
	//SIG_CHK_SUT();

	// Mkey is created without block signature support
	mkey_dv_new<mkey_basic_attr<>,
		    mkey_layout_new_list_mrs<DATA_SIZE>,
		    mkey_sig_block_none>
		src_mkey(*this, this->src_side.pd, 1, MLX5DV_MKEY_INIT_ATTR_FLAGS_INDIRECT);

	EXECL(src_mkey.init());

	EXEC(src_side.qp.wr_flags(IBV_SEND_SIGNALED | IBV_SEND_INLINE));
	EXEC(src_side.qp.wr_start());
	EXECL(src_mkey.wr_configure(this->src_side.qp));
	EXEC(src_side.qp.wr_complete(EOPNOTSUPP));
}
