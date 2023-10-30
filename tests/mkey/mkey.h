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

#ifndef MKEY_H
#define MKEY_H

#include <algorithm>
#include <string>
#include <ostream>
#include <stdint.h>
#include <endian.h>

#define INITL(x) do { \
		if (!this->env.skip) { \
			VERBS_TRACE("%3d.%p: initialize\t%s" #x "\n", __LINE__, this, this->env.lvl_str); \
			this->env.lvl_str[this->env.lvl++] = ' '; \
			EXPECT_NO_FATAL_FAILURE(x); \
			this->env.lvl_str[--this->env.lvl] = 0; \
			if (this->env.skip) { \
				VERBS_TRACE("%3d.%p: failed\t%s" #x " - skipping test\n", __LINE__, this, this->env.lvl_str); \
				return; \
			} \
		} \
	} while(0)

#if HAVE_DECL_MLX5DV_WR_MKEY_CONFIGURE

struct dif {
	uint16_t guard;
	uint16_t app_tag;
	uint32_t ref_tag;
};

typedef union {
	uint64_t sig; // sig is in Big Endian (Network) mode
	struct dif dif;
} dif_to_sig;

template <uint16_t Guard, uint16_t AppTag, uint32_t RefTag,
	  bool RefRemap = true>
struct t10dif_sig {
	static const uint16_t guard = Guard;
	static const uint16_t app_tag = AppTag;
	static const uint32_t ref_tag = RefTag;

	static void sig_to_buf(uint8_t *buf, uint32_t block_index) {
		dif_to_sig dif;

		dif.dif.guard = htons(guard);
		dif.dif.app_tag = htons(app_tag);

		if (RefRemap) {
			dif.dif.ref_tag = htonl(ref_tag + block_index);
		} else {
			dif.dif.ref_tag = htonl(ref_tag);
		}

		*(uint64_t *)buf = dif.sig;
	}
};

struct sig_none {
	static void sig_to_buf(uint8_t *buf, uint32_t block_index) {}
};

template <uint32_t Sig> struct crc32_sig {
	static const uint32_t sig = Sig;

	static void sig_to_buf(uint8_t *buf, uint32_t block_index) {
		*(uint32_t *)buf = htonl(sig);
	}
};

template <uint64_t Sig> struct crc64_sig {
	static const uint64_t sig = Sig;

	static void sig_to_buf(uint8_t *buf, uint32_t block_index) {
		*(uint64_t *)buf = htobe64(sig);
	}
};

#if HAVE_DECL_MLX5DV_SIG_TYPE_NVMEDIF

#define _MASK(N_BITS) (((N_BITS) == 64) ? UINT64_MAX : (((1ULL << (N_BITS)) - 1)))
template <uint64_t Guard,
	  uint64_t StorageTag,
	  uint64_t RefTag,
	  uint16_t AppTag,
	  int Format,
	  uint8_t StorageTagSize,
	  int Flags = 0>
struct nvmedif_sig {
	static const uint64_t guard = Guard;
	static const uint64_t storage_tag = StorageTag;
	static const uint64_t _ref_tag = RefTag;
	static const uint16_t app_tag = AppTag;
	static const int format = Format;
	static const uint8_t sts = StorageTagSize;
	static const int flags = Flags;

	static void sig_to_buf(uint8_t *buf, uint32_t block_index) {
		int i = 0;
		uint64_t sts_mask;
		uint64_t ref_tag;
		uint64_t ref_tag_mask;
		uint8_t ref_tag_size;

		if (flags & MLX5DV_SIG_NVMEDIF_FLAG_REF_REMAP) {
			ref_tag = _ref_tag + block_index;
		} else {
			ref_tag = _ref_tag;
		}

		static_assert((format == MLX5DV_SIG_NVMEDIF_FORMAT_16 && sts <= 32) ||
			      (format == MLX5DV_SIG_NVMEDIF_FORMAT_32 && sts >= 16 && sts <= 64) ||
			      (format == MLX5DV_SIG_NVMEDIF_FORMAT_64 && sts <= 48));

		if (format == MLX5DV_SIG_NVMEDIF_FORMAT_16) {
			*(uint16_t *)buf = htons(guard);
			i += 2;
			ref_tag_size = 32 - sts;
		} else if (format == MLX5DV_SIG_NVMEDIF_FORMAT_32) {
			*(uint32_t *)buf = htonl(guard);
			i += 4;
			ref_tag_size = 80 - sts;
		} else { // format == MLX5DV_SIG_NVMEDIF_FORMAT_64
			*(uint64_t *)buf = htobe64(guard);
			i += 8;
			ref_tag_size = 48 - sts;
		}
		ref_tag_mask = _MASK(ref_tag_size);

		*(uint16_t *)&buf[i] = htons(app_tag);
		i += 2;

		if (format == MLX5DV_SIG_NVMEDIF_FORMAT_16) {
			if (sts == 0) {
				*(uint32_t *)&buf[i] = htonl(ref_tag & ref_tag_mask);
			} else {
				sts_mask = _MASK(sts);
				*(uint32_t *)&buf[i] = htonl(((storage_tag & sts_mask) << ref_tag_size) |
							     (ref_tag & ref_tag_mask));
			}
		} else if (format == MLX5DV_SIG_NVMEDIF_FORMAT_32) {

			buf[i++] = (storage_tag >> (sts - 8 )) & 0xff;
			buf[i++] = (storage_tag >> (sts - 16)) & 0xff;

			if (sts == 16) {
				*(uint64_t *)&buf[i] = htobe64(ref_tag & ref_tag_mask);
			} else {
				sts_mask = _MASK(sts - 16);
                                *(uint64_t *)&buf[i] = htobe64(((storage_tag & sts_mask) << ref_tag_size) |
							       (ref_tag & ref_tag_mask));
                        }
                } else { // format == MLX5DV_SIG_NVMEDIF_FORMAT_64
			uint64_t storage_and_ref_space;

			if (sts == 0) {
				storage_and_ref_space = ref_tag & ref_tag_mask;
			} else {
				sts_mask = _MASK(sts);
				storage_and_ref_space = ((storage_tag & sts_mask) << ref_tag_size) |
							(ref_tag & ref_tag_mask);
			}

			buf[i++] = (storage_and_ref_space >> 40) & 0xff;
			buf[i++] = (storage_and_ref_space >> 32) & 0xff;
			buf[i++] = (storage_and_ref_space >> 24) & 0xff;
			buf[i++] = (storage_and_ref_space >> 16) & 0xff;
			buf[i++] = (storage_and_ref_space >> 8 ) & 0xff;
			buf[i++] = (storage_and_ref_space      ) & 0xff;
		}
	}
};

template <uint64_t Guard,
	  uint64_t StorageTag,
	  uint64_t RefTag,
	  uint16_t AppTag,
	  uint8_t StorageTagSize,
	  int Flags = 0>
struct nvmedif_16_sig : nvmedif_sig<Guard, StorageTag, RefTag, AppTag,
				    MLX5DV_SIG_NVMEDIF_FORMAT_16,
				    StorageTagSize, Flags> {
};

template <uint64_t Guard,
	  uint64_t StorageTag,
	  uint64_t RefTag,
	  uint16_t AppTag,
	  uint8_t StorageTagSize,
	  int Flags = 0>
struct nvmedif_32_sig : nvmedif_sig<Guard, StorageTag, RefTag, AppTag,
				    MLX5DV_SIG_NVMEDIF_FORMAT_32,
				    StorageTagSize, Flags> {
};

template <uint64_t Guard,
	  uint64_t StorageTag,
	  uint64_t RefTag,
	  uint16_t AppTag,
	  uint8_t StorageTagSize,
	  int Flags = 0>
struct nvmedif_64_sig : nvmedif_sig<Guard, StorageTag, RefTag, AppTag,
				    MLX5DV_SIG_NVMEDIF_FORMAT_64,
				    StorageTagSize, Flags> {
};

#undef _MASK
#endif /* HAVE_DECL_MLX5DV_SIG_TYPE_NVMEDIF */
#endif /* HAVE_DECL_MLX5DV_WR_MKEY_CONFIGURE */

template<uint32_t MaxSendWr = 128, uint32_t MaxSendSge = 16,
	 uint32_t MaxRecvWr = 32, uint32_t MaxRecvSge = 4,
	 uint32_t MaxInlineData = 512, bool Pipelining = false,
	 uint64_t SendOpsFlags = IBV_QP_EX_WITH_RDMA_WRITE |
				 IBV_QP_EX_WITH_SEND |
				 IBV_QP_EX_WITH_RDMA_READ |
				 IBV_QP_EX_WITH_LOCAL_INV,
	 uint64_t DvSendOpsFlags = MLX5DV_QP_EX_WITH_MR_INTERLEAVED |
				   MLX5DV_QP_EX_WITH_MR_LIST
#if HAVE_DECL_MLX5DV_WR_MKEY_CONFIGURE
				   | MLX5DV_QP_EX_WITH_MKEY_CONFIGURE
#endif
				   >
struct ibvt_qp_dv : public ibvt_qp_rc {
	ibvt_qp_dv(ibvt_env &e, ibvt_pd &p, ibvt_cq &c) :
		ibvt_qp_rc(e, p, c) {}

	virtual void init_attr(struct ibv_qp_init_attr_ex &attr) override {
		ibvt_qp_rc::init_attr(attr);
		attr.cap.max_send_wr = MaxSendWr;
		attr.cap.max_send_sge = MaxSendSge;
		attr.cap.max_recv_wr = MaxRecvWr;
		attr.cap.max_recv_sge = MaxRecvSge;
		attr.cap.max_inline_data = MaxInlineData;
		if (SendOpsFlags) {
			attr.comp_mask |= IBV_QP_INIT_ATTR_SEND_OPS_FLAGS;
			attr.send_ops_flags = SendOpsFlags;
		}
	}

	virtual void init_dv_attr(struct mlx5dv_qp_init_attr &dv_attr) {
		if (DvSendOpsFlags) {
			dv_attr.comp_mask = MLX5DV_QP_INIT_ATTR_MASK_SEND_OPS_FLAGS;
			dv_attr.send_ops_flags = DvSendOpsFlags;
		}
#if HAVE_DECL_MLX5DV_WR_MKEY_CONFIGURE
		if (Pipelining) {
			dv_attr.comp_mask |=
			    MLX5DV_QP_INIT_ATTR_MASK_QP_CREATE_FLAGS;
			dv_attr.create_flags = MLX5DV_QP_CREATE_SIG_PIPELINING;
		}
#endif /* HAVE_DECL_MLX5DV_WR_MKEY_CONFIGURE */
	}

	virtual void init() override {
		struct ibv_qp_init_attr_ex attr = {};
		struct mlx5dv_qp_init_attr dv_attr = {};

		INIT(pd.init());
		INIT(cq.init());

		init_attr(attr);
		init_dv_attr(dv_attr);
		SET(qp, mlx5dv_create_qp(pd.ctx.ctx, &attr, &dv_attr));
	}

	virtual void wr_start() {
		struct ibv_qp_ex *qpx = ibv_qp_to_qp_ex(qp);
		EXECL(ibv_wr_start(qpx));
	}

	virtual void wr_complete(int status = 0) {
		struct ibv_qp_ex *qpx = ibv_qp_to_qp_ex(qp);
		ASSERT_EQ(status, ibv_wr_complete(qpx));
	}

	virtual void wr_id(uint64_t id) {
		ibv_qp_to_qp_ex(qp)->wr_id = id;
	}

	virtual void wr_flags(unsigned int flags) {
		ibv_qp_to_qp_ex(qp)->wr_flags = flags;
	}

	virtual void wr_rdma_read(struct ibv_sge local_sge, struct ibv_sge remote_sge) {
		struct ibv_qp_ex *qpx = ibv_qp_to_qp_ex(qp);
		ibv_wr_rdma_read(qpx, remote_sge.lkey, remote_sge.addr);
		ibv_wr_set_sge_list(qpx, 1, &local_sge);
	}

	virtual void wr_rdma_write(struct ibv_sge local_sge, struct ibv_sge remote_sge) {
		struct ibv_qp_ex *qpx = ibv_qp_to_qp_ex(qp);
		ibv_wr_rdma_write(qpx, remote_sge.lkey, remote_sge.addr);
		ibv_wr_set_sge_list(qpx, 1, &local_sge);
	}

	virtual void wr_send(struct ibv_sge local_sge) {
		struct ibv_qp_ex *qpx = ibv_qp_to_qp_ex(qp);
		ibv_wr_send(qpx);
		ibv_wr_set_sge_list(qpx, 1, &local_sge);
	}

#if HAVE_DECL_MLX5DV_WR_MKEY_CONFIGURE
	virtual void cancel_posted_wrs(uint64_t wr_id, int wr_num) {
		struct mlx5dv_qp_ex *dv_qp;
		struct ibv_qp_ex *qpx = ibv_qp_to_qp_ex(qp);

		dv_qp = mlx5dv_qp_ex_from_ibv_qp_ex(qpx);
		int ret = mlx5dv_qp_cancel_posted_send_wrs(dv_qp, wr_id);
		ASSERT_EQ(wr_num, ret);
	}
#endif /* HAVE_DECL_MLX5DV_WR_MKEY_CONFIGURE */

	virtual void modify_qp_to_rts() {
		struct ibv_qp_attr attr;

		memset(&attr, 0, sizeof(attr));

		attr.qp_state = IBV_QPS_RTS;
		attr.cur_qp_state = IBV_QPS_SQD;
		DO(ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_CUR_STATE));
	}
};

struct mkey : public ibvt_abstract_mr {
	ibvt_pd &pd;

	mkey(ibvt_env &env, ibvt_pd &pd) :
		ibvt_abstract_mr(env, 0, 0),
		pd(pd) {}

	virtual ~mkey() = default;

	virtual void init() = 0;

	virtual void wr_configure(ibvt_qp &qp) = 0;

	virtual void configure(ibvt_qp &qp) {
		struct ibv_qp_ex *qpx = ibv_qp_to_qp_ex(qp.qp);

		EXECL(ibv_wr_start(qpx));
		EXEC(wr_configure(qp));
		DO(ibv_wr_complete(qpx));
	}

	virtual void wr_invalidate(ibvt_qp &qp) = 0;

	virtual void invalidate(ibvt_qp &qp) {
		struct ibv_qp_ex *qpx = ibv_qp_to_qp_ex(qp.qp);

		EXECL(ibv_wr_start(qpx));
		EXEC(wr_invalidate(qp));
		DO(ibv_wr_complete(qpx));
	}

	virtual struct ibv_sge sge(intptr_t start, size_t length) override {
		struct ibv_sge ret = {};

		ret.addr = start;
		ret.length = length;
		ret.lkey = lkey();

		return ret;
	}

	virtual void check() = 0;
	virtual void check(int err_type) = 0;
	virtual void check(int err_type, uint64_t actual, uint64_t expected,
			   uint64_t offset) = 0;
	virtual void inc() = 0;
};

#if HAVE_DECL_MLX5DV_WR_MKEY_CONFIGURE
struct mkey_sig_err {
	enum mlx5dv_mkey_err_type err_type;
	bool is_err_info_valid;
	struct mlx5dv_sig_err err_info;

	mkey_sig_err(enum mlx5dv_mkey_err_type e) :
		err_type(e),
		is_err_info_valid(false) {}

	mkey_sig_err(int e) :
		err_type((enum mlx5dv_mkey_err_type)e),
		is_err_info_valid(false) { }

	mkey_sig_err(const struct mlx5dv_mkey_err *dv_err) {
		err_type = dv_err->err_type;
		if (err_type == MLX5DV_MKEY_NO_ERR) {
			is_err_info_valid = false;
		} else {
			is_err_info_valid = true;
			err_info.actual_value = dv_err->err.sig.actual_value;
			err_info.expected_value = dv_err->err.sig.expected_value;
			err_info.offset = dv_err->err.sig.offset;
		}
	}

	mkey_sig_err(int et, uint64_t actual, uint64_t expected,
		     uint64_t offset) {
		err_type = (enum mlx5dv_mkey_err_type)et;
		is_err_info_valid = true;
		err_info.actual_value = actual;
		err_info.expected_value = expected;
		err_info.offset = offset;
	}

	const char *type_c_str() const {
		switch(err_type) {
		case MLX5DV_MKEY_NO_ERR:
			return "MLX5DV_MKEY_NO_ERR";
		case MLX5DV_MKEY_SIG_BLOCK_BAD_GUARD:
			return "MLX5DV_MKEY_SIG_BLOCK_BAD_GUARD";
		case MLX5DV_MKEY_SIG_BLOCK_BAD_REFTAG:
			return "MLX5DV_MKEY_SIG_BLOCK_BAD_REFTAG";
		case MLX5DV_MKEY_SIG_BLOCK_BAD_APPTAG:
			return "MLX5DV_MKEY_SIG_BLOCK_BAD_APPTAG";
#if HAVE_DECL_MLX5DV_SIG_TYPE_NVMEDIF
		case MLX5DV_MKEY_SIG_BLOCK_BAD_STORAGETAG:
			return "MLX5DV_MKEY_SIG_BLOCK_BAD_STORAGETAG";
#endif
		}
		return "UNKNOWN_ERROR";
	}

	std::string type_str() const {
		return std::string(type_c_str());
	}

	uint64_t actual() const {
		return err_info.actual_value;
	}

	uint64_t expected() const {
		return err_info.expected_value;
	}

	uint64_t offset() const {
		return err_info.offset;
	}

	friend bool operator==(const mkey_sig_err &l, const mkey_sig_err &r) {
		if (l.err_type != r.err_type)
			return false;

		if (l.is_err_info_valid && r.is_err_info_valid) {
			if (l.actual() != r.actual())
				return false;

			if (l.expected() != r.expected())
				return false;

			if (l.offset() != r.offset())
				return false;
		}

		return true;
	}

	friend std::ostream& operator<<(std::ostream& os, const mkey_sig_err &sig_err) {
		std::ios_base::fmtflags tmp;

		if (!sig_err.is_err_info_valid)
			return os << sig_err.type_str();

		tmp = os.flags();
		os << sig_err.type_str() << " actual: 0x" << std::hex << sig_err.actual() <<
			", expected: 0x" << sig_err.expected() << ", offset: " << std::dec <<
			sig_err.offset();
		os.flags(tmp);

		return os;
	}
};
#endif

struct mkey_dv : public mkey {
	const uint16_t max_entries;
	const uint32_t create_flags;
	struct mlx5dv_mkey *mlx5_mkey;

	mkey_dv(ibvt_env &env, ibvt_pd &pd, uint16_t me, uint32_t cf) :
		mkey(env, pd),
		max_entries(me),
		create_flags(cf),
		mlx5_mkey(NULL) {}

	virtual void init() override {
		struct mlx5dv_mkey_init_attr in = {};
		if (mlx5_mkey)
			return;

		in.pd = pd.pd;
		in.max_entries = max_entries;
		in.create_flags = create_flags;
		SET(mlx5_mkey, mlx5dv_create_mkey(&in));
	}

	virtual ~mkey_dv() {
		FREE(mlx5dv_destroy_mkey, mlx5_mkey);
	}

	virtual void wr_invalidate(ibvt_qp &qp) override {
		struct ibv_qp_ex *qpx = ibv_qp_to_qp_ex(qp.qp);
		EXECL(ibv_wr_local_inv(qpx, mlx5_mkey->lkey));
	}

	virtual uint32_t lkey() override {
		return mlx5_mkey->lkey;
	}

#if HAVE_DECL_MLX5DV_WR_MKEY_CONFIGURE
	virtual void check(int err_type) override {
		struct mlx5dv_mkey_err err;
		DO(mlx5dv_mkey_check(mlx5_mkey, &err));
		ASSERT_EQ(mkey_sig_err(err_type), mkey_sig_err(&err));
	}

	virtual void check() override {
		check(MLX5DV_MKEY_NO_ERR);
	}

	virtual void check(int err_type, uint64_t actual_value, uint64_t expected_value,
			   uint64_t offset) override {
		struct mlx5dv_mkey_err err;
		DO(mlx5dv_mkey_check(mlx5_mkey, &err));
		mkey_sig_err expected(err_type, actual_value, expected_value, offset);
		mkey_sig_err actual(&err);
		ASSERT_EQ(expected, actual);
	}
#else
	virtual void check() override {
	}

	virtual void check(int err_type) override {
	}

	virtual void check(int err_type, uint64_t actual, uint64_t expected,
			   uint64_t offset) override {
	}
#endif /* HAVE_DECL_MLX5DV_WR_MKEY_CONFIGURE */

	virtual void inc() override {
#if HAVE_DECL_MLX5DV_MKEY_INIT_ATTR_FLAGS_UPDATE_TAG
		mlx5_mkey->rkey = ibv_inc_rkey(mlx5_mkey->rkey);
		mlx5_mkey->lkey = mlx5_mkey->rkey;
#else
		FAIL();
#endif
	}
};

#if HAVE_DECL_MLX5DV_WR_MKEY_CONFIGURE
struct mkey_setter {
	virtual ~mkey_setter() = default;
	virtual void init() {};
	virtual void wr_set(ibvt_qp &qp) = 0;
	virtual size_t adjust_length(size_t length) { return length; };
};

template<uint32_t AccessFlags = IBV_ACCESS_LOCAL_WRITE |
	 IBV_ACCESS_REMOTE_READ |
	 IBV_ACCESS_REMOTE_WRITE>
struct mkey_access_flags : public mkey_setter {
	uint32_t access_flags;
	/* @todo: add comp_mask attr */

	mkey_access_flags(ibvt_env &env, ibvt_pd &pd, uint32_t access_flags = AccessFlags) :
		access_flags(access_flags) {}

	virtual void wr_set(struct ibvt_qp &qp) {
		struct ibv_qp_ex *qpx = ibv_qp_to_qp_ex(qp.qp);
		struct mlx5dv_qp_ex *mqp = mlx5dv_qp_ex_from_ibv_qp_ex(qpx);

		mlx5dv_wr_set_mkey_access_flags(mqp, access_flags);
	}
};

struct mkey_layout_new : public mkey_setter {
	virtual ~mkey_layout_new() = default;
	virtual size_t data_length() = 0;
	virtual void set_data(const uint8_t *buf, size_t length) = 0;
	virtual void get_data(uint8_t *buf, size_t length) = 0;
	virtual void dump(size_t offset = 0, size_t length = 0, const char *pfx = "") {}
};

struct mkey_layout_new_list : public mkey_layout_new {
	std::vector<struct ibv_sge> sgl;

	mkey_layout_new_list() :
		sgl() {}

	virtual ~mkey_layout_new_list() = default;

	void init(std::initializer_list<struct ibv_sge> l) {
		sgl = l;
	}

	void init(std::vector<struct ibv_sge> l) {
		sgl = l;
	}

	virtual size_t data_length() override {
		size_t len = 0;

		for (const struct ibv_sge &sge : sgl) {
			len += sge.length;
		}

		return len;
	}

	virtual void wr_set(ibvt_qp &qp) override {
		struct ibv_qp_ex *qpx = ibv_qp_to_qp_ex(qp.qp);
		struct mlx5dv_qp_ex *mqp = mlx5dv_qp_ex_from_ibv_qp_ex(qpx);

		mlx5dv_wr_set_mkey_layout_list(mqp, sgl.size(), sgl.data());
	}

	/* @todo: will not work on top of other mkey where addr is zero. */
	virtual void set_data(const uint8_t *buf, size_t length) override {
		for (const auto &sge : sgl) {
			memcpy((void *)sge.addr, buf, std::min((size_t)sge.length, length));
			if (length <= sge.length)
				break;
			length -= sge.length;
			buf += sge.length;
		}
	}

	virtual void get_data(uint8_t *buf, size_t length) override {
		for (const auto &sge : sgl) {
			memcpy(buf, (void *)sge.addr, std::min((size_t)sge.length, length));
			if (length <= sge.length)
				break;
			length -= sge.length;
			buf += sge.length;
		}
	}
};

struct _mkey_layout_new_list_mrs : public mkey_layout_new_list {
	ibvt_env &env;
	ibvt_pd &pd;
	std::vector<struct ibvt_mr> mrs;
	bool initialized;

	_mkey_layout_new_list_mrs(ibvt_env &env, ibvt_pd &pd) :
		env(env), pd(pd), initialized(false) {}

	virtual ~_mkey_layout_new_list_mrs() = default;

	virtual void init(std::vector<size_t> &sizes) {
		if (initialized)
			return;

		initialized = true;
		std::vector<struct ibv_sge> sgl;

		for (auto &s: sizes) {
			mrs.emplace_back(env, pd, s);
			auto &mr = mrs.back();
			mr.init();
			mr.fill();
			sgl.push_back(mr.sge());
		}

		mkey_layout_new_list::init(sgl);
	}

	virtual void dump(size_t offset = 0, size_t length = 0, const char *pfx = "") override {
		for (auto &mr : mrs) {
			mr.dump(offset, std::min(length, mr.size), pfx);
			length -= mr.size;
		}
	}
};

template<size_t ...Sizes>
struct mkey_layout_new_list_mrs : public _mkey_layout_new_list_mrs {

	mkey_layout_new_list_mrs(ibvt_env &env, ibvt_pd &pd) :
		_mkey_layout_new_list_mrs(env, pd) {}

	virtual ~mkey_layout_new_list_mrs() = default;

	virtual void init() override {
		std::vector<size_t> sizes = { Sizes... };

		_mkey_layout_new_list_mrs::init(sizes);
	}
};

template<size_t Size, size_t Count>
struct mkey_layout_new_list_fixed_mrs : public _mkey_layout_new_list_mrs {

	mkey_layout_new_list_fixed_mrs(ibvt_env &env, ibvt_pd &pd) :
		_mkey_layout_new_list_mrs(env, pd) {}

	virtual ~mkey_layout_new_list_fixed_mrs() = default;

	virtual void init() override {
		std::vector<size_t> sizes;

		for (size_t i = 0; i < Count; ++i)
			sizes.push_back(Size);

		_mkey_layout_new_list_mrs::init(sizes);
	}
};

struct mkey_layout_new_interleaved : public mkey_layout_new {
	uint16_t repeat_count;
	std::vector<struct mlx5dv_mr_interleaved> interleaved;

	mkey_layout_new_interleaved() :
		repeat_count(0),
		interleaved({}) {}

	virtual ~mkey_layout_new_interleaved() = default;

	void init(uint32_t rc,
		  std::initializer_list<struct mlx5dv_mr_interleaved> &i) {
		repeat_count = rc;
		interleaved = i;
	}

	void init(uint32_t rc,
		  std::vector<struct mlx5dv_mr_interleaved> &i) {
		repeat_count = rc;
		interleaved = i;
	}

	virtual size_t data_length() override {
		size_t len = 0;

		for (const struct mlx5dv_mr_interleaved &i : interleaved) {
			len += i.bytes_count;
		}

		len *= repeat_count;
		return len;
	}

	virtual void wr_set(ibvt_qp &qp) override {
		struct ibv_qp_ex *qpx = ibv_qp_to_qp_ex(qp.qp);
		struct mlx5dv_qp_ex *mqp = mlx5dv_qp_ex_from_ibv_qp_ex(qpx);

		mlx5dv_wr_set_mkey_layout_interleaved(mqp,
						      repeat_count,
						      interleaved.size(),
						      interleaved.data());
	}

	/* @todo: will not work on top of other mkey where addr is zero. */
	virtual void set_data(const uint8_t *buf, size_t length) override {
		auto tmp_interleaved = interleaved;
		for (uint16_t r = 0; r < repeat_count; ++r) {
			for (auto &i : tmp_interleaved) {
				memcpy((void *)i.addr, buf, std::min((size_t)i.bytes_count, length));
				if (length <= i.bytes_count)
					break;
				length -= i.bytes_count;
				buf += i.bytes_count;
				i.addr += i.bytes_count + i.bytes_skip;
			}
		}
	}

	virtual void get_data(uint8_t *buf, size_t length) override {
		auto tmp_interleaved = interleaved;
		for (uint16_t r = 0; r < repeat_count; ++r) {
			for (auto &i : tmp_interleaved) {
				memcpy(buf, (void *)i.addr, std::min((size_t)i.bytes_count, length));
				if (length <= i.bytes_count)
					break;
				length -= i.bytes_count;
				buf += i.bytes_count;
				i.addr += i.bytes_count + i.bytes_skip;
			}
		}
	}
};

template<uint32_t RepeatCount, uint32_t ...Interleaved>
struct mkey_layout_new_interleaved_mrs : public mkey_layout_new_interleaved {
	ibvt_env &env;
	ibvt_pd &pd;
	std::vector<struct ibvt_mr> mrs;
	bool initialized;

	mkey_layout_new_interleaved_mrs(ibvt_env &env, ibvt_pd &pd) :
		env(env), pd(pd), initialized(false) {}

	virtual ~mkey_layout_new_interleaved_mrs() = default;

	virtual void init() override {
		if (initialized)
			return;

		initialized = true;
		std::initializer_list<uint32_t> tmp_interleaved = { Interleaved... };
		std::vector<struct mlx5dv_mr_interleaved> mlx5_interleaved;

		static_assert(sizeof...(Interleaved) % 2 == 0, "Number of interleaved is not multiple of 2");
		for (auto i = tmp_interleaved.begin(); i != tmp_interleaved.end(); ++i) {
			auto byte_count = *i;
			auto skip_count = *(++i);
			mrs.emplace_back(env, pd, RepeatCount * (byte_count + skip_count));
			struct ibvt_mr &mr = mrs.back();
			mr.init();
			mr.fill();
			mlx5_interleaved.push_back({ (uint64_t)mr.buff, byte_count, skip_count, mr.lkey() });
		}

		mkey_layout_new_interleaved::init(RepeatCount, mlx5_interleaved);
	}

	virtual void dump(size_t offset = 0, size_t length = 0, const char *pfx = "") override {
		for (auto &mr : mrs) {
			mr.dump(offset, std::min(length, mr.size), pfx);
			length -= mr.size;
		}
	}
};

template<enum mlx5dv_sig_t10dif_bg_type BgType, enum mlx5dv_sig_t10dif_bg_caps BgTypeCaps>
struct mkey_sig_t10dif_type {
	static const enum mlx5dv_sig_t10dif_bg_type mlx5_t10dif_type = BgType;
	static const enum mlx5dv_sig_t10dif_bg_caps mlx5_t10dif_caps = BgTypeCaps;
};

typedef mkey_sig_t10dif_type<MLX5DV_SIG_T10DIF_CRC, MLX5DV_SIG_T10DIF_BG_CAP_CRC> mkey_sig_t10dif_crc;
typedef mkey_sig_t10dif_type<MLX5DV_SIG_T10DIF_CSUM, MLX5DV_SIG_T10DIF_BG_CAP_CSUM> mkey_sig_t10dif_csum;

template<typename BgType, uint16_t Bg, uint16_t AppTag, uint32_t RefTag>
struct mkey_sig_t10dif_type1 {
	static constexpr uint32_t sig_size = 8;
	struct mlx5dv_sig_t10dif dif;

	void set_sig(struct mlx5dv_sig_block_domain &domain) {
		domain.sig_type = MLX5DV_SIG_TYPE_T10DIF;
		dif.bg_type = BgType::mlx5_t10dif_type;
		dif.bg = Bg;
		dif.app_tag = AppTag;
		dif.ref_tag = RefTag;
		dif.flags = MLX5DV_SIG_T10DIF_FLAG_REF_REMAP |
			    MLX5DV_SIG_T10DIF_FLAG_APP_ESCAPE;
		domain.sig.dif = &dif;
		domain.comp_mask = 0;
	}

	static bool is_supported(struct mlx5dv_context &attr) {
		return attr.sig_caps.t10dif_bg & BgType::mlx5_t10dif_caps &&
		       attr.sig_caps.block_prot & MLX5DV_SIG_PROT_CAP_T10DIF;
	}
};

template<typename BgType, uint16_t Bg, uint16_t AppTag, uint32_t RefTag>
struct mkey_sig_t10dif_type3 {
	static constexpr uint32_t sig_size = 8;
	struct mlx5dv_sig_t10dif dif;

	void set_sig(struct mlx5dv_sig_block_domain &domain) {
		domain.sig_type = MLX5DV_SIG_TYPE_T10DIF;
		dif.bg_type = BgType::mlx5_t10dif_type;
		dif.bg = Bg;
		dif.app_tag = AppTag;
		dif.ref_tag = RefTag;
		dif.flags = MLX5DV_SIG_T10DIF_FLAG_APP_REF_ESCAPE;
		domain.sig.dif = &dif;
		domain.comp_mask = 0;
	}

	static bool is_supported(struct mlx5dv_context &attr) {
		return attr.sig_caps.t10dif_bg & BgType::mlx5_t10dif_caps &&
		       attr.sig_caps.block_prot & MLX5DV_SIG_PROT_CAP_T10DIF;
	}
};

template<enum mlx5dv_sig_crc_type CrcType, enum mlx5dv_sig_crc_type_caps CrcTypeCaps>
struct mkey_sig_crc_type {
	static const enum mlx5dv_sig_crc_type mlx5_crc_type = CrcType;
	static const enum mlx5dv_sig_crc_type_caps mlx5_crc_type_caps = CrcTypeCaps;
};

typedef mkey_sig_crc_type<MLX5DV_SIG_CRC_TYPE_CRC32, MLX5DV_SIG_CRC_TYPE_CAP_CRC32> mkey_sig_crc_type_crc32;
typedef mkey_sig_crc_type<MLX5DV_SIG_CRC_TYPE_CRC32C, MLX5DV_SIG_CRC_TYPE_CAP_CRC32C> mkey_sig_crc_type_crc32c;
typedef mkey_sig_crc_type<MLX5DV_SIG_CRC_TYPE_CRC64_XP10, MLX5DV_SIG_CRC_TYPE_CAP_CRC64_XP10> mkey_sig_crc_type_crc64xp10;

template<typename CrcType, uint32_t Seed>
struct mkey_sig_crc32 {
	static constexpr uint32_t sig_size = 4;
	struct mlx5dv_sig_crc crc;

	void set_sig(struct mlx5dv_sig_block_domain &domain) {
		domain.sig_type = MLX5DV_SIG_TYPE_CRC;
		crc.type = CrcType::mlx5_crc_type;
		crc.seed = Seed;
		domain.sig.crc = &crc;
		domain.comp_mask = 0;
	}

	static bool is_supported(struct mlx5dv_context &attr) {
		return attr.sig_caps.crc_type & CrcType::mlx5_crc_type_caps &&
		       attr.sig_caps.block_prot & MLX5DV_SIG_PROT_CAP_CRC;
	}
};

template<typename CrcType, uint64_t Seed>
struct mkey_sig_crc64 {
	static constexpr uint32_t sig_size = 8;
	struct mlx5dv_sig_crc crc;

	void set_sig(struct mlx5dv_sig_block_domain &domain) {
		domain.sig_type = MLX5DV_SIG_TYPE_CRC;
		crc.type = CrcType::mlx5_crc_type;
		crc.seed = Seed;
		domain.sig.crc = &crc;
		domain.comp_mask = 0;
	}

	static bool is_supported(struct mlx5dv_context &attr) {
		return attr.sig_caps.crc_type & CrcType::mlx5_crc_type_caps &&
		       attr.sig_caps.block_prot & MLX5DV_SIG_PROT_CAP_CRC;
	}
};

#if HAVE_DECL_MLX5DV_SIG_TYPE_NVMEDIF

template <uint64_t Seed,
	  uint64_t StorageTag,
	  uint64_t RefTag,
	  uint16_t AppTag,
	  mlx5dv_sig_nvmedif_format Format,
	  uint8_t StorageTagSize,
	  uint16_t Flags = 0,
	  uint8_t AppTagCheck = 0xf,
	  uint8_t StorageTagCheck = 0x3f>
struct mkey_sig_nvmedif {
	static constexpr uint32_t sig_size = (Format == MLX5DV_SIG_NVMEDIF_FORMAT_16) ? 8 : 16;
	struct mlx5dv_sig_nvmedif nvmedif;

	void set_sig(struct mlx5dv_sig_block_domain &domain) {
		nvmedif.format = Format;
		nvmedif.flags = Flags;
		nvmedif.seed = Seed;
		nvmedif.storage_tag = StorageTag;
		nvmedif.ref_tag = RefTag;
		nvmedif.app_tag = AppTag;
		nvmedif.sts = StorageTagSize;
		nvmedif.app_tag_check = AppTagCheck;
		nvmedif.storage_tag_check = StorageTagCheck;

		domain.sig_type = MLX5DV_SIG_TYPE_NVMEDIF;
		domain.sig.nvmedif = &nvmedif;
		domain.comp_mask = 0;
	}

	static bool is_supported(struct mlx5dv_context &attr) {
		return attr.sig_caps.block_prot & MLX5DV_SIG_PROT_CAP_NVMEDIF;
	}
};

template <uint64_t Seed,
	  uint64_t StorageTag,
	  uint64_t RefTag,
	  uint16_t AppTag,
	  uint8_t StorageTagSize,
	  uint16_t Flags = 0,
	  uint8_t AppTagCheck = 0xf,
	  uint8_t StorageTagCheck = 0x3f>
struct mkey_sig_nvmedif_16 : mkey_sig_nvmedif<Seed, StorageTag, RefTag, AppTag, MLX5DV_SIG_NVMEDIF_FORMAT_16,
					      StorageTagSize, Flags, AppTagCheck, StorageTagCheck> {};

template <uint64_t Seed,
	  uint64_t StorageTag,
	  uint64_t RefTag,
	  uint16_t AppTag,
	  uint8_t StorageTagSize,
	  uint16_t Flags = 0,
	  uint8_t AppTagCheck = 0xf,
	  uint8_t StorageTagCheck = 0x3f>
struct mkey_sig_nvmedif_32 : mkey_sig_nvmedif<Seed, StorageTag, RefTag, AppTag, MLX5DV_SIG_NVMEDIF_FORMAT_32,
					      StorageTagSize, Flags, AppTagCheck, StorageTagCheck> {};

template <uint64_t Seed,
	  uint64_t StorageTag,
	  uint64_t RefTag,
	  uint16_t AppTag,
	  uint8_t StorageTagSize,
	  uint16_t Flags = 0,
	  uint8_t AppTagCheck = 0xf,
	  uint8_t StorageTagCheck = 0x3f>
struct mkey_sig_nvmedif_64 : mkey_sig_nvmedif<Seed, StorageTag, RefTag, AppTag, MLX5DV_SIG_NVMEDIF_FORMAT_64,
					      StorageTagSize, Flags, AppTagCheck, StorageTagCheck> {};

#endif /* HAVE_DECL_MLX5DV_SIG_TYPE_NVMEDIF */

template<enum mlx5dv_block_size Mlx5BlockSize,
	 enum mlx5dv_block_size_caps Mlx5BlockSizeCaps,
	 uint32_t BlockSize>
struct mkey_block_size {
	static const enum mlx5dv_block_size mlx5_block_size = Mlx5BlockSize;
	static const enum mlx5dv_block_size_caps mlx5_block_size_caps = Mlx5BlockSizeCaps;
	static const uint32_t block_size = BlockSize;
};

typedef mkey_block_size<MLX5DV_BLOCK_SIZE_512, MLX5DV_BLOCK_SIZE_CAP_512, 512> mkey_block_size_512;
typedef mkey_block_size<MLX5DV_BLOCK_SIZE_520, MLX5DV_BLOCK_SIZE_CAP_520, 520> mkey_block_size_520;
typedef mkey_block_size<MLX5DV_BLOCK_SIZE_4048, MLX5DV_BLOCK_SIZE_CAP_4048, 4048> mkey_block_size_4048;
typedef mkey_block_size<MLX5DV_BLOCK_SIZE_4096, MLX5DV_BLOCK_SIZE_CAP_4096, 4096> mkey_block_size_4096;
typedef mkey_block_size<MLX5DV_BLOCK_SIZE_4160, MLX5DV_BLOCK_SIZE_CAP_4160, 4160> mkey_block_size_4160;

template<typename Sig, typename BlockSize>
struct mkey_sig_block_domain {
	typedef BlockSize BlockSizeType;
	typedef Sig SigType;

	struct mlx5dv_sig_block_domain domain;
	Sig sig;

	void set_domain(const mlx5dv_sig_block_domain **d) {
		sig.set_sig(domain);
		domain.block_size = BlockSize::mlx5_block_size;
		*d = &domain;
	}

	static bool is_supported(struct mlx5dv_context &attr) {
		return attr.sig_caps.block_size & BlockSizeType::mlx5_block_size_caps &&
			SigType::is_supported(attr);
	}
};

struct mkey_sig_block_domain_none {
	typedef mkey_block_size_512 BlockSizeType;
	typedef struct mkey_sig_none {
		static constexpr uint32_t sig_size = 0;
	} SigType;

	void set_domain(const mlx5dv_sig_block_domain **d) {
		*d = NULL;
	}

	static bool is_supported(struct mlx5dv_context &attr) {
		return true;
	}
};

#define MLX5DV_SIG_CHECK_T10DIF_APPTAG_BYTE1 0x20
#define MLX5DV_SIG_CHECK_T10DIF_APPTAG_BYTE0 0x10

template<typename MemDomain, typename WireDomain,
	 uint8_t CheckMask = 0xFF,
	 uint16_t Flags = 0,
	 uint8_t CopyMask = 0xFF>
struct mkey_sig_block : public mkey_setter {
	typedef MemDomain MemDomainType;
	typedef WireDomain WireDomainType;

	mkey_sig_block(ibvt_env &env, ibvt_pd &pd) {}

	virtual void wr_set(ibvt_qp &qp) override {
		struct ibv_qp_ex *qpx = ibv_qp_to_qp_ex(qp.qp);
		struct mlx5dv_qp_ex *mqp = mlx5dv_qp_ex_from_ibv_qp_ex(qpx);
		struct mlx5dv_sig_block_attr attr = {};

		MemDomain mem;
		WireDomain wire;
		mem.set_domain(&attr.mem);
		wire.set_domain(&attr.wire);
		attr.flags = Flags;
		attr.check_mask = CheckMask;
		attr.copy_mask = CopyMask;
		mlx5dv_wr_set_mkey_sig_block(mqp, &attr);
	}

	virtual size_t adjust_length(size_t length) {
		size_t mem_num_blocks = length / (MemDomainType::BlockSizeType::block_size + MemDomainType::SigType::sig_size);
		size_t data_length = length - mem_num_blocks * MemDomainType::SigType::sig_size;
		size_t wire_num_blocks = data_length / WireDomainType::BlockSizeType::block_size;
		size_t wire_length = data_length + wire_num_blocks * WireDomainType::SigType::sig_size;
		return wire_length;
	}

	static bool is_supported(struct mlx5dv_context &attr) {
		return attr.comp_mask & MLX5DV_CONTEXT_MASK_SIGNATURE_OFFLOAD &&
			MemDomainType::is_supported(attr) &&
			WireDomainType::is_supported(attr);
	}
};

// Some helper types
typedef mkey_sig_crc32<mkey_sig_crc_type_crc32, 0> mkey_sig_crc32ieee;
typedef mkey_sig_crc32<mkey_sig_crc_type_crc32c, 0> mkey_sig_crc32c;
typedef mkey_sig_crc64<mkey_sig_crc_type_crc64xp10, 0> mkey_sig_crc64xp10;
typedef mkey_sig_t10dif_type1<mkey_sig_t10dif_crc, 0xffff, 0x5678, 0xf0debc9a> mkey_sig_t10dif_crc_type1_default;
typedef mkey_sig_t10dif_type3<mkey_sig_t10dif_crc, 0xffff, 0x5678, 0xf0debc9a> mkey_sig_t10dif_crc_type3_default;

typedef mkey_sig_t10dif_type1<mkey_sig_t10dif_csum, 0xffff, 0x5678, 0xf0debc9a> mkey_sig_t10dif_csum_type1_default;
typedef mkey_sig_t10dif_type3<mkey_sig_t10dif_csum, 0xffff, 0x5678, 0xf0debc9a> mkey_sig_t10dif_csum_type3_default;

#if HAVE_DECL_MLX5DV_SIG_TYPE_NVMEDIF
typedef mkey_sig_nvmedif_16<UINT16_MAX, 0, 0x89abcdef, 0x4567, 0> mkey_sig_nvmedif_16_sts_0_default;
typedef mkey_sig_nvmedif_16<UINT16_MAX, 0x89ab, 0xcdef, 0x4567, 16> mkey_sig_nvmedif_16_sts_16_default;
typedef mkey_sig_nvmedif_16<UINT16_MAX, 0x89abcdef, 0, 0x4567, 32> mkey_sig_nvmedif_16_sts_32_default;

typedef mkey_sig_nvmedif_32<0, 0xcdef, 0x0123456789abcdef, 0x89ab, 16> mkey_sig_nvmedif_32_sts_16_default;

typedef mkey_sig_nvmedif_64<0, 0x4567, 0x89abcdef, 0x0123, 16> mkey_sig_nvmedif_64_sts_16_default;
#endif /* HAVE_DECL_MLX5DV_SIG_TYPE_NVMEDIF */

typedef mkey_sig_block<mkey_sig_block_domain_none, mkey_sig_block_domain_none> mkey_sig_block_none;

template<typename ...Setters>
struct mkey_dv_new : public mkey_dv {
	struct mkey_layout_new *layout;
	std::vector<struct mkey_setter *> preset_setters;
	std::vector<struct mkey_setter *> setters;
	bool initialized;

	mkey_dv_new(ibvt_env &env, ibvt_pd &pd, uint16_t me, uint32_t cf) :
		mkey_dv(env, pd, me, cf),
		layout(NULL),
		initialized(false) {
		create_setters(new Setters(env, pd)...);
	}

	virtual ~mkey_dv_new() {
		for (auto s : preset_setters) {
			delete s;
		}
	}

	virtual void init() override {
		if (initialized)
			return;

		initialized = true;
		mkey_dv::init();
		if (layout) layout->init();
	}

	void create_setters() {}

	template<typename Setter, typename ...Rest>
	void create_setters(Setter *setter, Rest * ...rest) {
		preset_setters.push_back(setter);
		add_setter(setter);
		if (std::is_base_of<struct mkey_layout_new, Setter>::value) {
			layout = dynamic_cast<mkey_layout_new *>(setter);
		}
		create_setters(rest...);
	}

	void set_layout(struct mkey_layout_new *layout) {
		this->layout = layout;
		add_setter(layout);
	}

	void add_setter(struct mkey_setter *setter) {
		setters.push_back(setter);
	}

	virtual void wr_configure(ibvt_qp &qp) override {
		struct ibv_qp_ex *qpx = ibv_qp_to_qp_ex(qp.qp);
		struct mlx5dv_qp_ex *mqp = mlx5dv_qp_ex_from_ibv_qp_ex(qpx);
		struct mlx5dv_mkey_conf_attr attr = {};

		EXECL(mlx5dv_wr_mkey_configure(mqp, mlx5_mkey, setters.size(), &attr));
		for (auto s : setters) {
			EXECL(s->wr_set(qp));
		}
	}

	virtual struct ibv_sge sge() override {
		size_t length = layout ? layout->data_length() : 0;
		for (auto s : setters) {
			length = s->adjust_length(length);
		}
		return mkey_dv::sge(0, length);
	}

	virtual void dump(size_t offset = 0, size_t length = 0, const char *pfx = "") override {
		if (layout) layout->dump(offset, length, pfx);
	}
};
#endif /* HAVE_DECL_MLX5DV_WR_MKEY_CONFIGURE */

template<typename QP>
struct mkey_test_side : public ibvt_obj {
	ibvt_pd pd;
	ibvt_cq cq;
	QP qp;

	mkey_test_side(ibvt_env &env, ibvt_ctx &ctx) :
		ibvt_obj(env),
		pd(env, ctx),
		cq(env, ctx),
		qp(env, pd, cq) {}

	virtual void init() {
		INIT(qp.init());
	}

	virtual void connect(struct mkey_test_side &remote) {
		qp.connect(&remote.qp);
	}

	void trigger_poll() {
		struct ibv_cq_ex *cq_ex = cq.cq2();
		struct ibv_poll_cq_attr attr = {};

		ASSERT_EQ(ENOENT, ibv_start_poll(cq_ex, &attr));
	}
};

struct rdma_op {
	template<typename QP>
	void check_completion(mkey_test_side<QP> &side, enum ibv_wc_status status = IBV_WC_SUCCESS) {
		ibvt_wc wc(side.cq);
		side.cq.do_poll(wc);
		ASSERT_EQ(status, wc().status);
	}

	template<typename QP>
	void check_completion(mkey_test_side<QP> &side,
				     enum ibv_wc_opcode opcode,
				     enum ibv_wc_status status = IBV_WC_SUCCESS) {
		ibvt_wc wc(side.cq);
		side.cq.do_poll(wc);
		ASSERT_EQ(status, wc().status);
		ASSERT_EQ(opcode, wc().opcode);
	}
};

struct rdma_op_write : rdma_op {
	template<typename QP>
	void wr_submit(mkey_test_side<QP> &src_side,
			       ibv_sge src_sge,
			       mkey_test_side<QP> &dst_side,
			       ibv_sge dst_sge) {
		src_side.qp.wr_flags(IBV_SEND_SIGNALED);
		src_side.qp.wr_rdma_write(src_sge, dst_sge);
	}

	template<typename QP>
	void submit(mkey_test_side<QP> &src_side,
			    ibv_sge src_sge,
			    mkey_test_side<QP> &dst_side,
			    ibv_sge dst_sge) {
		src_side.qp.wr_start();
		wr_submit(src_side, src_sge, dst_side, dst_sge);
		src_side.qp.wr_complete();
	}

	template<typename QP>
	void complete(mkey_test_side<QP> &src_side,
			      mkey_test_side<QP> &dst_side,
			      enum ibv_wc_status src_status = IBV_WC_SUCCESS,
			      enum ibv_wc_status dst_status = IBV_WC_SUCCESS) {
		this->check_completion(src_side, src_status);
		dst_side.trigger_poll();
	}
};

struct rdma_op_read : rdma_op {
	template<typename QP>
	void wr_submit(mkey_test_side<QP> &src_side,
			       ibv_sge src_sge,
			       mkey_test_side<QP> &dst_side,
			       ibv_sge dst_sge) {
		dst_side.qp.wr_flags(IBV_SEND_SIGNALED);
		dst_side.qp.wr_rdma_read(dst_sge, src_sge);
	}

	template<typename QP>
	void submit(mkey_test_side<QP> &src_side,
			    ibv_sge src_sge,
			    mkey_test_side<QP> &dst_side,
			    ibv_sge dst_sge) {
		dst_side.qp.wr_start();
		wr_submit(src_side, src_sge, dst_side, dst_sge);
		dst_side.qp.wr_complete();
	}

	template<typename QP>
	void complete(mkey_test_side<QP> &src_side,
			      mkey_test_side<QP> &dst_side,
			      enum ibv_wc_status src_status = IBV_WC_SUCCESS,
			      enum ibv_wc_status dst_status = IBV_WC_SUCCESS) {
		this->check_completion(dst_side, dst_status);
		src_side.trigger_poll();
	}
};

struct rdma_op_send : rdma_op {
	template<typename QP>
	void wr_submit(mkey_test_side<QP> &src_side,
			       ibv_sge src_sge,
			       mkey_test_side<QP> &dst_side,
			       ibv_sge dst_sge) {
		// @todo: chaining for recv part is not implemented
		dst_side.qp.recv(dst_sge);
		src_side.qp.wr_flags(IBV_SEND_SIGNALED);
		src_side.qp.wr_send(src_sge);
	}

	template<typename QP>
	void submit(mkey_test_side<QP> &src_side,
			    ibv_sge src_sge,
			    mkey_test_side<QP> &dst_side,
			    ibv_sge dst_sge) {
		dst_side.qp.recv(dst_sge);
		src_side.qp.wr_start();
		wr_submit(src_side, src_sge, dst_side, dst_sge);
		src_side.qp.wr_complete();
	}

	template<typename QP>
	void complete(mkey_test_side<QP> &src_side,
			      mkey_test_side<QP> &dst_side,
			      enum ibv_wc_status src_status = IBV_WC_SUCCESS,
			      enum ibv_wc_status dst_status = IBV_WC_SUCCESS) {
		this->check_completion(src_side, src_status);
		this->check_completion(dst_side, dst_status);
	}

};

template<typename Qp>
struct mkey_test_base : public testing::Test, public ibvt_env {
	ibvt_ctx ctx;
	struct mkey_test_side<Qp> src_side;
	struct mkey_test_side<Qp> dst_side;

	mkey_test_base() :
		ctx(*this, NULL),
		src_side(*this, ctx),
		dst_side(*this, ctx) {}

	virtual void SetUp() override {
		INIT(ctx.init());
		INIT(src_side.init());
		INIT(dst_side.init());
		INIT(src_side.connect(dst_side));
		INIT(dst_side.connect(src_side));
	}

	virtual void TearDown() override {
		ASSERT_FALSE(HasFailure());
	}
};

#endif /* MKEY_H */
