/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 *  (C) 2014 Mellanox Technologies, Inc.
 *
 */



#include "mxm_impl.h"

enum {
    MXM_MPICH_ISEND,
    MXM_MPICH_ISEND_SYNC,
    MXM_MPICH_ISEND_AM
};


static int _mxm_handle_sreq(MPID_Request * req);
static void _mxm_send_completion_cb(void *context);
static int _mxm_isend(MPID_nem_mxm_ep_t * ep, MPID_nem_mxm_req_area * req,
                      int type, mxm_mq_h mxm_mq, int mxm_rank, int id, mxm_tag_t tag, int block);
static int _mxm_process_sdtype(MPID_Request ** rreq_p, MPI_Datatype datatype,
                               MPID_Datatype * dt_ptr, MPIDI_msg_sz_t data_sz, const void *buf,
                               int count, mxm_req_buffer_t ** iov_buf, int *iov_count);


#undef FUNCNAME
#define FUNCNAME MPID_nem_mxm_iSendContig
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_mxm_iSendContig(MPIDI_VC_t * vc, MPID_Request * sreq, void *hdr, MPIDI_msg_sz_t hdr_sz,
                             void *data, MPIDI_msg_sz_t data_sz)
{
    int mpi_errno = MPI_SUCCESS;

    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_MXM_ISENDCONTIGMSG);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_MXM_ISENDCONTIGMSG);

    MPIU_Assert(hdr_sz <= sizeof(MPIDI_CH3_Pkt_t));
    MPIU_DBG_MSG(CH3_CHANNEL, VERBOSE, "mxm_iSendContig");
    MPIDI_DBG_Print_packet((MPIDI_CH3_Pkt_t *) hdr);

    MPIU_Memcpy(&(sreq->dev.pending_pkt), (char *) hdr, sizeof(MPIDI_CH3_Pkt_t));
    sreq->dev.tmpbuf = NULL;

    REQ_FIELD(sreq, ctx) = sreq;
    REQ_FIELD(sreq, iov_buf) = REQ_FIELD(sreq, tmp_buf);
    REQ_FIELD(sreq, iov_count) = 1;
    REQ_FIELD(sreq, iov_buf)[0].ptr = (void *) &(sreq->dev.pending_pkt);
    REQ_FIELD(sreq, iov_buf)[0].length = sizeof(MPIDI_CH3_Pkt_t);
    if (data_sz) {
        REQ_FIELD(sreq, iov_count) = 2;
        REQ_FIELD(sreq, iov_buf)[1].ptr = (void *) data;
        REQ_FIELD(sreq, iov_buf)[1].length = data_sz;
    }

    _dbg_mxm_output(5, "iSendContig ========> Sending ADI msg for req %p (data_size %d, %d) \n",
                    sreq, sizeof(MPIDI_CH3_Pkt_t), data_sz);

    (VC_FIELD(vc, pending_sends)) += 1;
    sreq->ch.vc = vc;
    sreq->ch.noncontig = FALSE;

    mpi_errno = _mxm_isend(VC_FIELD(vc, mxm_ep), REQ_BASE(sreq), MXM_MPICH_ISEND_AM,
                           mxm_obj->mxm_mq, mxm_obj->mxm_rank, MXM_MPICH_HID_ADI_MSG, 0, 0);
    if (mpi_errno)
        MPIU_ERR_POP(mpi_errno);

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_MXM_ISENDCONTIGMSG);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPID_nem_mxm_iStartContigMsg
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_mxm_iStartContigMsg(MPIDI_VC_t * vc, void *hdr, MPIDI_msg_sz_t hdr_sz, void *data,
                                 MPIDI_msg_sz_t data_sz, MPID_Request ** sreq_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *sreq = NULL;

    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_MXM_ISTARTCONTIGMSG);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_MXM_ISTARTCONTIGMSG);

    MPIU_Assert(hdr_sz <= sizeof(MPIDI_CH3_Pkt_t));
    MPIU_DBG_MSG(CH3_CHANNEL, VERBOSE, "mxm_iStartContigMsg");
    MPIDI_DBG_Print_packet((MPIDI_CH3_Pkt_t *) hdr);

    /* create a request */
    sreq = MPID_Request_create();
    MPIU_Assert(sreq != NULL);
    MPIU_Object_set_ref(sreq, 2);
    MPIU_Memcpy(&(sreq->dev.pending_pkt), (char *) hdr, sizeof(MPIDI_CH3_Pkt_t));
    sreq->kind = MPID_REQUEST_SEND;
    sreq->dev.OnDataAvail = NULL;
    sreq->dev.tmpbuf = NULL;

    _dbg_mxm_output(5, "iStartContigMsg ========> Sending ADI msg for req %p (data_size %d, %d) \n",
                    sreq, sizeof(MPIDI_CH3_Pkt_t), data_sz);

    REQ_FIELD(sreq, ctx) = sreq;
    REQ_FIELD(sreq, iov_buf) = REQ_FIELD(sreq, tmp_buf);
    REQ_FIELD(sreq, iov_count) = 1;
    REQ_FIELD(sreq, iov_buf)[0].ptr = (void *) &(sreq->dev.pending_pkt);
    REQ_FIELD(sreq, iov_buf)[0].length = sizeof(MPIDI_CH3_Pkt_t);
    if (data_sz) {
        REQ_FIELD(sreq, iov_count) = 2;
        REQ_FIELD(sreq, iov_buf)[1].ptr = (void *) data;
        REQ_FIELD(sreq, iov_buf)[1].length = data_sz;
    }

    (VC_FIELD(vc, pending_sends)) += 1;
    sreq->ch.vc = vc;
    sreq->ch.noncontig = FALSE;

    mpi_errno = _mxm_isend(VC_FIELD(vc, mxm_ep), REQ_BASE(sreq), MXM_MPICH_ISEND_AM,
                           mxm_obj->mxm_mq, mxm_obj->mxm_rank, MXM_MPICH_HID_ADI_MSG, 0, 0);
    if (mpi_errno)
        MPIU_ERR_POP(mpi_errno);

  fn_exit:
    *sreq_ptr = sreq;
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_MX_ISTARTCONTIGMSG);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPID_nem_mxm_SendNoncontig
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_mxm_SendNoncontig(MPIDI_VC_t * vc, MPID_Request * sreq, void *hdr,
                               MPIDI_msg_sz_t hdr_sz)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_msg_sz_t last;

    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_MXM_SENDNONCONTIGMSG);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_MXM_SENDNONCONTIGMSG);

    MPIU_Assert(hdr_sz <= sizeof(MPIDI_CH3_Pkt_t));
    MPIU_DBG_MSG(CH3_CHANNEL, VERBOSE, "MPID_nem_mxm_iSendNoncontig");

    MPIU_Memcpy(&(sreq->dev.pending_pkt), (char *) hdr, sizeof(MPIDI_CH3_Pkt_t));
    sreq->dev.tmpbuf = NULL;

    _dbg_mxm_output(5, "SendNoncontig ========> Sending ADI msg for req %p (data_size %d, %d) \n",
                    sreq, sizeof(MPIDI_CH3_Pkt_t), sreq->dev.segment_size);

    REQ_FIELD(sreq, ctx) = sreq;
    REQ_FIELD(sreq, iov_buf) = REQ_FIELD(sreq, tmp_buf);
    REQ_FIELD(sreq, iov_count) = 1;
    REQ_FIELD(sreq, iov_buf)[0].ptr = (void *) &(sreq->dev.pending_pkt);
    REQ_FIELD(sreq, iov_buf)[0].length = sizeof(MPIDI_CH3_Pkt_t);

    MPIU_Assert(sreq->dev.segment_first == 0);
    last = sreq->dev.segment_size;
    if (last > 0) {
        sreq->dev.tmpbuf = MPIU_Malloc((size_t) sreq->dev.segment_size);
        MPIU_Assert(sreq->dev.tmpbuf);
        MPID_Segment_pack(sreq->dev.segment_ptr, sreq->dev.segment_first, &last, sreq->dev.tmpbuf);
        MPIU_Assert(last == sreq->dev.segment_size);

        REQ_FIELD(sreq, iov_count) = 2;
        REQ_FIELD(sreq, iov_buf)[1].ptr = sreq->dev.tmpbuf;
        REQ_FIELD(sreq, iov_buf)[1].length = last;
    }

    (VC_FIELD(vc, pending_sends)) += 1;
    sreq->ch.vc = vc;
    sreq->ch.noncontig = TRUE;

    mpi_errno = _mxm_isend(VC_FIELD(vc, mxm_ep), REQ_BASE(sreq), MXM_MPICH_ISEND_AM,
                           mxm_obj->mxm_mq, mxm_obj->mxm_rank, MXM_MPICH_HID_ADI_MSG, 0, 0);
    if (mpi_errno)
        MPIU_ERR_POP(mpi_errno);

  fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_MXM_SENDNONCONTIGMSG);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPID_nem_mxm_send
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_mxm_send(MPIDI_VC_t * vc, const void *buf, int count, MPI_Datatype datatype, int rank,
                      int tag, MPID_Comm * comm, int context_offset, MPID_Request ** sreq_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *sreq = NULL;
    MPID_Datatype *dt_ptr;
    int dt_contig;
    MPIDI_msg_sz_t data_sz;
    MPI_Aint dt_true_lb;

    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_MXM_SEND);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_MXM_SEND);

    MPIDI_Datatype_get_info(count, datatype, dt_contig, data_sz, dt_ptr, dt_true_lb);

    /* create a request */
    MPIDI_Request_create_sreq(sreq, mpi_errno, goto fn_exit);
    MPIU_Assert(sreq != NULL);
    MPIDI_Request_set_type(sreq, MPIDI_REQUEST_TYPE_SEND);
    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
    MPIDI_Request_set_seqnum(sreq, seqnum);
    if (HANDLE_GET_KIND(datatype) != HANDLE_KIND_BUILTIN) {
        MPID_Datatype_get_ptr(datatype, sreq->dev.datatype_ptr);
        MPID_Datatype_add_ref(sreq->dev.datatype_ptr);
    }
    sreq->partner_request = NULL;
    sreq->dev.OnDataAvail = NULL;
    sreq->dev.tmpbuf = NULL;
    sreq->ch.vc = vc;
    sreq->ch.noncontig = FALSE;

    _dbg_mxm_output(5,
                    "Send ========> Sending USER msg for req %p (context %d rank %d tag %d size %d) \n",
                    sreq, comm->context_id + context_offset, comm->rank, tag, data_sz);

    REQ_FIELD(sreq, ctx) = sreq;
    REQ_FIELD(sreq, iov_buf) = REQ_FIELD(sreq, tmp_buf);
    REQ_FIELD(sreq, iov_count) = 0;
    REQ_FIELD(sreq, iov_buf)[0].ptr = NULL;
    REQ_FIELD(sreq, iov_buf)[0].length = 0;

    if (data_sz) {
        if (dt_contig) {
            REQ_FIELD(sreq, iov_count) = 1;
            REQ_FIELD(sreq, iov_buf)[0].ptr = (char *) (buf) + dt_true_lb;
            REQ_FIELD(sreq, iov_buf)[0].length = data_sz;
        }
        else {
            MPIDI_msg_sz_t last;
            MPI_Aint packsize = 0;

            sreq->dev.segment_ptr = MPID_Segment_alloc();
            MPIU_ERR_CHKANDJUMP1((sreq->dev.segment_ptr == NULL), mpi_errno, MPI_ERR_OTHER,
                                 "**nomem", "**nomem %s", "MPID_Segment_alloc");
            MPIR_Pack_size_impl(count, datatype, &packsize);

            last = data_sz;
            if (packsize > 0) {
                sreq->dev.tmpbuf = MPIU_Malloc((size_t) packsize);
                MPIU_Assert(sreq->dev.tmpbuf);
                MPID_Segment_init(buf, count, datatype, sreq->dev.segment_ptr, 0);
                MPID_Segment_pack(sreq->dev.segment_ptr, 0, &last, sreq->dev.tmpbuf);

                REQ_FIELD(sreq, iov_count) = 1;
                REQ_FIELD(sreq, iov_buf)[0].ptr = sreq->dev.tmpbuf;
                REQ_FIELD(sreq, iov_buf)[0].length = last;
            }
            sreq->ch.noncontig = TRUE;
        }
    }

    (VC_FIELD(vc, pending_sends)) += 1;

    mpi_errno = _mxm_isend(VC_FIELD(vc, mxm_ep), REQ_BASE(sreq), MXM_MPICH_ISEND,
                           (mxm_mq_h) comm->ch.netmod_comm, comm->rank, tag, _mxm_tag_mpi2mxm(tag,
                                                                                              comm->
                                                                                              context_id
                                                                                              +
                                                                                              context_offset),
                           1);
    if (mpi_errno)
        MPIU_ERR_POP(mpi_errno);

    _dbg_mxm_out_req(sreq);

  fn_exit:
    *sreq_ptr = sreq;
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_MXM_SEND);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPID_nem_mxm_ssend
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_mxm_ssend(MPIDI_VC_t * vc, const void *buf, int count, MPI_Datatype datatype, int rank,
                       int tag, MPID_Comm * comm, int context_offset, MPID_Request ** sreq_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *sreq = NULL;
    MPID_Datatype *dt_ptr;
    int dt_contig;
    MPIDI_msg_sz_t data_sz;
    MPI_Aint dt_true_lb;

    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_MXM_SSEND);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_MXM_SSEND);

    MPIDI_Datatype_get_info(count, datatype, dt_contig, data_sz, dt_ptr, dt_true_lb);

    /* create a request */
    MPIDI_Request_create_sreq(sreq, mpi_errno, goto fn_exit);
    MPIU_Assert(sreq != NULL);
    MPIDI_Request_set_type(sreq, MPIDI_REQUEST_TYPE_SEND);
    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
    MPIDI_Request_set_seqnum(sreq, seqnum);
    if (HANDLE_GET_KIND(datatype) != HANDLE_KIND_BUILTIN) {
        MPID_Datatype_get_ptr(datatype, sreq->dev.datatype_ptr);
        MPID_Datatype_add_ref(sreq->dev.datatype_ptr);
    }
    sreq->partner_request = NULL;
    sreq->dev.OnDataAvail = NULL;
    sreq->dev.tmpbuf = NULL;
    sreq->ch.vc = vc;
    sreq->ch.noncontig = FALSE;

    _dbg_mxm_output(5,
                    "sSend ========> Sending USER msg for req %p (context %d rank %d tag %d size %d) \n",
                    sreq, comm->context_id + context_offset, comm->rank, tag, data_sz);

    REQ_FIELD(sreq, ctx) = sreq;
    REQ_FIELD(sreq, iov_buf) = REQ_FIELD(sreq, tmp_buf);
    REQ_FIELD(sreq, iov_count) = 0;
    REQ_FIELD(sreq, iov_buf)[0].ptr = NULL;
    REQ_FIELD(sreq, iov_buf)[0].length = 0;

    if (data_sz) {
        if (dt_contig) {
            REQ_FIELD(sreq, iov_count) = 1;
            REQ_FIELD(sreq, iov_buf)[0].ptr = (char *) (buf) + dt_true_lb;
            REQ_FIELD(sreq, iov_buf)[0].length = data_sz;
        }
        else {
            MPIDI_msg_sz_t last;
            MPI_Aint packsize = 0;

            sreq->dev.segment_ptr = MPID_Segment_alloc();
            MPIU_ERR_CHKANDJUMP1((sreq->dev.segment_ptr == NULL), mpi_errno, MPI_ERR_OTHER,
                                 "**nomem", "**nomem %s", "MPID_Segment_alloc");
            MPIR_Pack_size_impl(count, datatype, &packsize);

            last = data_sz;
            if (packsize > 0) {
                sreq->dev.tmpbuf = MPIU_Malloc((size_t) packsize);
                MPIU_Assert(sreq->dev.tmpbuf);
                MPID_Segment_init(buf, count, datatype, sreq->dev.segment_ptr, 0);
                MPID_Segment_pack(sreq->dev.segment_ptr, 0, &last, sreq->dev.tmpbuf);

                REQ_FIELD(sreq, iov_count) = 1;
                REQ_FIELD(sreq, iov_buf)[0].ptr = sreq->dev.tmpbuf;
                REQ_FIELD(sreq, iov_buf)[0].length = last;
            }
            sreq->ch.noncontig = TRUE;
        }
    }

    (VC_FIELD(vc, pending_sends)) += 1;

    mpi_errno = _mxm_isend(VC_FIELD(vc, mxm_ep), REQ_BASE(sreq), MXM_MPICH_ISEND_SYNC,
                           (mxm_mq_h) comm->ch.netmod_comm, comm->rank, tag, _mxm_tag_mpi2mxm(tag,
                                                                                              comm->
                                                                                              context_id
                                                                                              +
                                                                                              context_offset),
                           1);
    if (mpi_errno)
        MPIU_ERR_POP(mpi_errno);

    _dbg_mxm_out_req(sreq);

  fn_exit:
    *sreq_ptr = sreq;
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_MXM_SSEND);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPID_nem_mxm_isend
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_mxm_isend(MPIDI_VC_t * vc, const void *buf, int count, MPI_Datatype datatype, int rank,
                       int tag, MPID_Comm * comm, int context_offset, MPID_Request ** sreq_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *sreq = NULL;
    MPID_Datatype *dt_ptr;
    int dt_contig;
    MPIDI_msg_sz_t data_sz;
    MPI_Aint dt_true_lb;

    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_MXM_ISEND);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_MXM_ISEND);

    MPIDI_Datatype_get_info(count, datatype, dt_contig, data_sz, dt_ptr, dt_true_lb);

    /* create a request */
    MPIDI_Request_create_sreq(sreq, mpi_errno, goto fn_exit);
    MPIU_Assert(sreq != NULL);
    MPIDI_Request_set_type(sreq, MPIDI_REQUEST_TYPE_SEND);
    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
    MPIDI_Request_set_seqnum(sreq, seqnum);
    if (HANDLE_GET_KIND(datatype) != HANDLE_KIND_BUILTIN) {
        MPID_Datatype_get_ptr(datatype, sreq->dev.datatype_ptr);
        MPID_Datatype_add_ref(sreq->dev.datatype_ptr);
    }
    sreq->partner_request = NULL;
    sreq->dev.OnDataAvail = NULL;
    sreq->dev.tmpbuf = NULL;
    sreq->ch.vc = vc;
    sreq->ch.noncontig = FALSE;

    _dbg_mxm_output(5,
                    "iSend ========> Sending USER msg for req %p (context %d rank %d tag %d size %d) \n",
                    sreq, comm->context_id + context_offset, comm->rank, tag, data_sz);

    REQ_FIELD(sreq, ctx) = sreq;
    REQ_FIELD(sreq, iov_buf) = REQ_FIELD(sreq, tmp_buf);
    REQ_FIELD(sreq, iov_count) = 0;
    REQ_FIELD(sreq, iov_buf)[0].ptr = NULL;
    REQ_FIELD(sreq, iov_buf)[0].length = 0;

    if (data_sz) {
        if (dt_contig) {
            REQ_FIELD(sreq, iov_count) = 1;
            REQ_FIELD(sreq, iov_buf)[0].ptr = (char *) (buf) + dt_true_lb;
            REQ_FIELD(sreq, iov_buf)[0].length = data_sz;
        }
        else {
            MPIDI_msg_sz_t last;
            MPI_Aint packsize = 0;

            sreq->dev.segment_ptr = MPID_Segment_alloc();
            MPIU_ERR_CHKANDJUMP1((sreq->dev.segment_ptr == NULL), mpi_errno, MPI_ERR_OTHER,
                                 "**nomem", "**nomem %s", "MPID_Segment_alloc");
            MPIR_Pack_size_impl(count, datatype, &packsize);

            last = data_sz;
            if (packsize > 0) {
                sreq->dev.tmpbuf = MPIU_Malloc((size_t) packsize);
                MPIU_Assert(sreq->dev.tmpbuf);
                MPID_Segment_init(buf, count, datatype, sreq->dev.segment_ptr, 0);
                MPID_Segment_pack(sreq->dev.segment_ptr, 0, &last, sreq->dev.tmpbuf);

                REQ_FIELD(sreq, iov_count) = 1;
                REQ_FIELD(sreq, iov_buf)[0].ptr = sreq->dev.tmpbuf;
                REQ_FIELD(sreq, iov_buf)[0].length = last;
            }
            sreq->ch.noncontig = TRUE;
        }
    }

    (VC_FIELD(vc, pending_sends)) += 1;

    mpi_errno = _mxm_isend(VC_FIELD(vc, mxm_ep), REQ_BASE(sreq), MXM_MPICH_ISEND,
                           (mxm_mq_h) comm->ch.netmod_comm, comm->rank, tag, _mxm_tag_mpi2mxm(tag,
                                                                                              comm->
                                                                                              context_id
                                                                                              +
                                                                                              context_offset),
                           0);
    if (mpi_errno)
        MPIU_ERR_POP(mpi_errno);

    _dbg_mxm_out_req(sreq);

  fn_exit:
    *sreq_ptr = sreq;
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_MXM_ISEND);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPID_nem_mxm_issend
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_mxm_issend(MPIDI_VC_t * vc, const void *buf, int count, MPI_Datatype datatype,
                        int rank, int tag, MPID_Comm * comm, int context_offset,
                        MPID_Request ** sreq_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *sreq = NULL;
    MPID_Datatype *dt_ptr;
    int dt_contig;
    MPIDI_msg_sz_t data_sz;
    MPI_Aint dt_true_lb;

    MPIDI_STATE_DECL(MPID_STATE_MPID_NEM_MXM_ISSEND);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_NEM_MXM_ISSEND);

    MPIDI_Datatype_get_info(count, datatype, dt_contig, data_sz, dt_ptr, dt_true_lb);

    /* create a request */
    MPIDI_Request_create_sreq(sreq, mpi_errno, goto fn_exit);
    MPIU_Assert(sreq != NULL);
    MPIDI_Request_set_type(sreq, MPIDI_REQUEST_TYPE_SEND);
    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
    MPIDI_Request_set_seqnum(sreq, seqnum);
    if (HANDLE_GET_KIND(datatype) != HANDLE_KIND_BUILTIN) {
        MPID_Datatype_get_ptr(datatype, sreq->dev.datatype_ptr);
        MPID_Datatype_add_ref(sreq->dev.datatype_ptr);
    }
    sreq->partner_request = NULL;
    sreq->dev.OnDataAvail = NULL;
    sreq->dev.tmpbuf = NULL;
    sreq->ch.vc = vc;
    sreq->ch.noncontig = FALSE;

    _dbg_mxm_output(5,
                    "isSend ========> Sending USER msg for req %p (context %d rank %d tag %d size %d) \n",
                    sreq, comm->context_id + context_offset, comm->rank, tag, data_sz);

    REQ_FIELD(sreq, ctx) = sreq;
    REQ_FIELD(sreq, iov_buf) = REQ_FIELD(sreq, tmp_buf);
    REQ_FIELD(sreq, iov_count) = 0;
    REQ_FIELD(sreq, iov_buf)[0].ptr = NULL;
    REQ_FIELD(sreq, iov_buf)[0].length = 0;

    if (data_sz) {
        if (dt_contig) {
            REQ_FIELD(sreq, iov_count) = 1;
            REQ_FIELD(sreq, iov_buf)[0].ptr = (char *) (buf) + dt_true_lb;
            REQ_FIELD(sreq, iov_buf)[0].length = data_sz;
        }
        else {
            MPIDI_msg_sz_t last;
            MPI_Aint packsize = 0;

            sreq->dev.segment_ptr = MPID_Segment_alloc();
            MPIU_ERR_CHKANDJUMP1((sreq->dev.segment_ptr == NULL), mpi_errno, MPI_ERR_OTHER,
                                 "**nomem", "**nomem %s", "MPID_Segment_alloc");
            MPIR_Pack_size_impl(count, datatype, &packsize);

            last = data_sz;
            if (packsize > 0) {
                sreq->dev.tmpbuf = MPIU_Malloc((size_t) packsize);
                MPIU_Assert(sreq->dev.tmpbuf);
                MPID_Segment_init(buf, count, datatype, sreq->dev.segment_ptr, 0);
                MPID_Segment_pack(sreq->dev.segment_ptr, 0, &last, sreq->dev.tmpbuf);

                REQ_FIELD(sreq, iov_count) = 1;
                REQ_FIELD(sreq, iov_buf)[0].ptr = sreq->dev.tmpbuf;
                REQ_FIELD(sreq, iov_buf)[0].length = last;
            }
            sreq->ch.noncontig = TRUE;
        }
    }

    (VC_FIELD(vc, pending_sends)) += 1;

    mpi_errno = _mxm_isend(VC_FIELD(vc, mxm_ep), REQ_BASE(sreq), MXM_MPICH_ISEND_SYNC,
                           (mxm_mq_h) comm->ch.netmod_comm, comm->rank, tag, _mxm_tag_mpi2mxm(tag,
                                                                                              comm->
                                                                                              context_id
                                                                                              +
                                                                                              context_offset),
                           0);
    if (mpi_errno)
        MPIU_ERR_POP(mpi_errno);

    _dbg_mxm_out_req(sreq);

  fn_exit:
    *sreq_ptr = sreq;
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_NEM_MXM_ISSEND);
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


static int _mxm_handle_sreq(MPID_Request * req)
{
    int complete = FALSE;
    int (*reqFn) (MPIDI_VC_t *, MPID_Request *, int *);

    _dbg_mxm_output(5, "========> Completing SEND req %p status %d\n", req, req->status.MPI_ERROR);
    _dbg_mxm_out_buf(REQ_FIELD(req, iov_buf)[0].ptr,
                     (REQ_FIELD(req, iov_buf)[0].length >
                      16 ? 16 : REQ_FIELD(req, iov_buf)[0].length));

    (VC_FIELD(req->ch.vc, pending_sends)) -= 1;
    if (((req->dev.datatype_ptr != NULL) && (req->dev.tmpbuf != NULL))) {
        MPIU_Free(req->dev.tmpbuf);
    }

    if (REQ_FIELD(req, iov_count) > MXM_MPICH_MAX_IOV) {
        MPIU_Free(REQ_FIELD(req, iov_buf));
        REQ_FIELD(req, iov_buf) = REQ_FIELD(req, tmp_buf);
        REQ_FIELD(req, iov_count) = 0;
    }

    reqFn = req->dev.OnDataAvail;
    if (!reqFn) {
        MPIDI_CH3U_Request_complete(req);
        MPIU_DBG_MSG(CH3_CHANNEL, VERBOSE, ".... complete");
    }
    else {
        MPIDI_VC_t *vc = req->ch.vc;

        reqFn(vc, req, &complete);
        if (!complete) {
            MPIU_Assert(complete == TRUE);
        }
    }

    return complete;
}


static void _mxm_send_completion_cb(void *context)
{
    MPID_Request *req = (MPID_Request *) context;

    MPIU_Assert(req);
    MPIU_Assert((req->kind == MPID_REQUEST_SEND) || (req->kind == MPID_PREQUEST_SEND));
    _dbg_mxm_out_req(req);

    _mxm_to_mpi_status(REQ_FIELD(req, mxm_req->item.base.error), &req->status);

    list_enqueue(&VC_FIELD(req->ch.vc, mxm_ep->free_queue), &REQ_FIELD(req, mxm_req->queue));

    if (likely(!MPIR_STATUS_GET_CANCEL_BIT(req->status))) {
        _mxm_handle_sreq(req);
    }
}


static int _mxm_isend(MPID_nem_mxm_ep_t * ep, MPID_nem_mxm_req_area * req,
                      int type, mxm_mq_h mxm_mq, int mxm_rank, int id, mxm_tag_t mxm_tag, int block)
{
    int mpi_errno = MPI_SUCCESS;
    mxm_error_t ret = MXM_OK;
    mxm_send_req_t *mxm_sreq;
    list_head_t *free_queue = NULL;

    MPIU_Assert(ep);
    MPIU_Assert(req);

    free_queue = &ep->free_queue;
    req->mxm_req = list_dequeue_mxm_req(free_queue);
    if (!req->mxm_req) {
        list_grow_mxm_req(free_queue);
        req->mxm_req = list_dequeue_mxm_req(free_queue);
        if (!req->mxm_req) {
            MPIU_DBG_MSG(CH3_CHANNEL, VERBOSE, "empty free queue");
            mpi_errno = MPI_ERR_OTHER;
            goto fn_fail;
        }
    }
    mxm_sreq = &(req->mxm_req->item.send);

    mxm_sreq->base.state = MXM_REQ_NEW;
    mxm_sreq->base.mq = mxm_mq;
    mxm_sreq->base.conn = ep->mxm_conn;
    mxm_sreq->base.completed_cb = _mxm_send_completion_cb;
    mxm_sreq->base.context = req->ctx;

    if (type == MXM_MPICH_ISEND_AM) {
        mxm_sreq->opcode = MXM_REQ_OP_AM;
        mxm_sreq->flags = 0;

        mxm_sreq->op.am.hid = id;
        mxm_sreq->op.am.imm_data = mxm_rank;
    }
    else if (type == MXM_MPICH_ISEND_SYNC) {
        mxm_sreq->opcode = MXM_REQ_OP_SEND_SYNC;
        mxm_sreq->flags = 0;

        mxm_sreq->op.send.tag = mxm_tag;
        mxm_sreq->op.send.imm_data = mxm_rank;
    }
    else {
        mxm_sreq->opcode = MXM_REQ_OP_SEND;
        mxm_sreq->flags = 0;

        mxm_sreq->op.send.tag = mxm_tag;
        mxm_sreq->op.send.imm_data = mxm_rank;
    }

    if (likely(req->iov_count == 1)) {
        mxm_sreq->base.data_type = MXM_REQ_DATA_BUFFER;
        mxm_sreq->base.data.buffer.ptr = req->iov_buf[0].ptr;
        mxm_sreq->base.data.buffer.length = req->iov_buf[0].length;
    }
    else {
        mxm_sreq->base.data_type = MXM_REQ_DATA_IOV;
        mxm_sreq->base.data.iov.vector = req->iov_buf;
        mxm_sreq->base.data.iov.count = req->iov_count;
    }

    ret = mxm_req_send(mxm_sreq);
    if (MXM_OK != ret) {
        list_enqueue(&ep->free_queue, &req->mxm_req->queue);
        mpi_errno = MPI_ERR_OTHER;
        goto fn_fail;
    }

    if (block)
        _mxm_req_wait(&mxm_sreq->base);
//    list_enqueue(&ep->out_queue, &req->mxm_req->queue);

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}


static int _mxm_process_sdtype(MPID_Request ** sreq_p, MPI_Datatype datatype,
                               MPID_Datatype * dt_ptr, MPIDI_msg_sz_t data_sz, const void *buf,
                               int count, mxm_req_buffer_t ** iov_buf, int *iov_count)
{
    int mpi_errno = MPI_SUCCESS;
    MPID_Request *sreq = *sreq_p;
    MPIDI_msg_sz_t last;
    MPID_IOV *iov;
    int n_iov = 0;
    int index;
    int size_to_copy = 0;

    sreq->dev.segment_ptr = MPID_Segment_alloc();
    MPIU_ERR_CHKANDJUMP1((sreq->dev.segment_ptr == NULL), mpi_errno, MPI_ERR_OTHER, "**nomem",
                         "**nomem %s", "MPID_Segment_alloc");

    MPID_Segment_init(buf, count, datatype, sreq->dev.segment_ptr, 0);
    sreq->dev.segment_first = 0;
    sreq->dev.segment_size = data_sz;

    last = sreq->dev.segment_size;
    MPID_Segment_count_contig_blocks(sreq->dev.segment_ptr, sreq->dev.segment_first, &last,
                                     (MPI_Aint *) & n_iov);
    MPIU_Assert(n_iov > 0);
    iov = MPIU_Malloc(n_iov * sizeof(*iov));
    MPIU_Assert(iov);

    last = sreq->dev.segment_size;
    MPID_Segment_pack_vector(sreq->dev.segment_ptr, sreq->dev.segment_first, &last, iov, &n_iov);
    MPIU_Assert(last == sreq->dev.segment_size);

    if (n_iov > MXM_MPICH_MAX_IOV) {
        *iov_buf = (mxm_req_buffer_t *) MPIU_Malloc(n_iov * sizeof(**iov_buf));
        MPIU_Assert(*iov_buf);
    }

    for (index = 0; index < n_iov; index++) {
        if (index < (MXM_REQ_DATA_MAX_IOV - 1)) {
            (*iov_buf)[index].ptr = iov[index].MPID_IOV_BUF;
            (*iov_buf)[index].length = iov[index].MPID_IOV_LEN;
        }
        else {
            size_to_copy += iov[index].MPID_IOV_LEN;
        }
    }

    if (size_to_copy == 0) {
        sreq->dev.tmpbuf = NULL;
        sreq->dev.tmpbuf_sz = 0;
        *iov_count = n_iov;
    }
    else {
        int offset = 0;
        sreq->dev.tmpbuf = MPIU_Malloc(size_to_copy);
        sreq->dev.tmpbuf_sz = size_to_copy;
        MPIU_Assert(sreq->dev.tmpbuf);
        for (index = (MXM_REQ_DATA_MAX_IOV - 1); index < n_iov; index++) {
            MPIU_Memcpy((char *) (sreq->dev.tmpbuf) + offset, iov[index].MPID_IOV_BUF,
                        iov[index].MPID_IOV_LEN);
            offset += iov[index].MPID_IOV_LEN;
        }
        (*iov_buf)[MXM_REQ_DATA_MAX_IOV - 1].ptr = sreq->dev.tmpbuf;
        (*iov_buf)[MXM_REQ_DATA_MAX_IOV - 1].length = size_to_copy;
        *iov_count = MXM_REQ_DATA_MAX_IOV;
    }
    MPIU_Free(iov);

  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}
