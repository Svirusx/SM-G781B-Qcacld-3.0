/*
 * Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * DOC: Implement various notification handlers which are accessed
 * internally in pkt_capture component only.
 */

#ifdef WLAN_FEATURE_PKT_CAPTURE_LITHIUM
#include <dp_types.h>
#endif
#include "wlan_pkt_capture_main.h"
#include "cfg_ucfg_api.h"
#include "wlan_pkt_capture_mon_thread.h"
#include "wlan_pkt_capture_mgmt_txrx.h"
#include "target_if_pkt_capture.h"
#include "cdp_txrx_ctrl.h"
#include "wlan_pkt_capture_tgt_api.h"

static struct wlan_objmgr_vdev *gp_pkt_capture_vdev;

#ifdef WLAN_FEATURE_PKT_CAPTURE_LITHIUM
wdi_event_subscribe PKT_CAPTURE_TX_SUBSCRIBER;
wdi_event_subscribe PKT_CAPTURE_RX_SUBSCRIBER;
wdi_event_subscribe PKT_CAPTURE_OFFLOAD_TX_SUBSCRIBER;

/**
 * pkt_capture_wdi_event_subscribe() - Subscribe pkt capture callbacks
 * @psoc: pointer to psoc object
 *
 * Return: None
 */
static void pkt_capture_wdi_event_subscribe(struct wlan_objmgr_psoc *psoc)
{
	void *soc = cds_get_context(QDF_MODULE_ID_SOC);
	uint8_t pdev_id = WMI_PDEV_ID_SOC;

	/* subscribing for tx data packets */
	PKT_CAPTURE_TX_SUBSCRIBER.callback =
				pkt_capture_callback;

	PKT_CAPTURE_TX_SUBSCRIBER.context = wlan_psoc_get_dp_handle(psoc);

	cdp_wdi_event_sub(soc, pdev_id, &PKT_CAPTURE_TX_SUBSCRIBER,
			  WDI_EVENT_PKT_CAPTURE_TX_DATA);

	/* subscribing for rx data packets */
	PKT_CAPTURE_RX_SUBSCRIBER.callback =
				pkt_capture_callback;

	PKT_CAPTURE_RX_SUBSCRIBER.context = wlan_psoc_get_dp_handle(psoc);

	cdp_wdi_event_sub(soc, pdev_id, &PKT_CAPTURE_RX_SUBSCRIBER,
			  WDI_EVENT_PKT_CAPTURE_RX_DATA);

	/* subscribing for offload tx data packets */
	PKT_CAPTURE_OFFLOAD_TX_SUBSCRIBER.callback =
				pkt_capture_callback;

	PKT_CAPTURE_OFFLOAD_TX_SUBSCRIBER.context =
						wlan_psoc_get_dp_handle(psoc);

	cdp_wdi_event_sub(soc, pdev_id, &PKT_CAPTURE_OFFLOAD_TX_SUBSCRIBER,
			  WDI_EVENT_PKT_CAPTURE_OFFLOAD_TX_DATA);
}

/**
 * pkt_capture_wdi_event_unsubscribe() - Unsubscribe pkt capture callbacks
 * @psoc: pointer to psoc object
 *
 * Return: None
 */
static void pkt_capture_wdi_event_unsubscribe(struct wlan_objmgr_psoc *psoc)
{
	void *soc = cds_get_context(QDF_MODULE_ID_SOC);
	uint8_t pdev_id = WMI_PDEV_ID_SOC;

	/* unsubscribing for tx data packets */
	cdp_wdi_event_unsub(soc, pdev_id, &PKT_CAPTURE_TX_SUBSCRIBER,
			    WDI_EVENT_PKT_CAPTURE_TX_DATA);

	/* unsubscribing for rx data packets */
	cdp_wdi_event_unsub(soc, pdev_id, &PKT_CAPTURE_RX_SUBSCRIBER,
			    WDI_EVENT_PKT_CAPTURE_RX_DATA);

	/* unsubscribing for offload tx data packets */
	cdp_wdi_event_unsub(soc, pdev_id, &PKT_CAPTURE_OFFLOAD_TX_SUBSCRIBER,
			    WDI_EVENT_PKT_CAPTURE_OFFLOAD_TX_DATA);
}

enum pkt_capture_mode
pkt_capture_get_pktcap_mode_lithium()
{
	enum pkt_capture_mode mode = PACKET_CAPTURE_MODE_DISABLE;
	struct pkt_capture_vdev_priv *vdev_priv;
	struct wlan_objmgr_vdev *vdev;

	vdev = pkt_capture_get_vdev();
	if (!vdev)
		return PACKET_CAPTURE_MODE_DISABLE;

	vdev_priv = pkt_capture_vdev_get_priv(vdev);
	if (!vdev_priv)
		pkt_capture_err("vdev_priv is NULL");
	else
		mode = vdev_priv->cb_ctx->pkt_capture_mode;

	return mode;
}

void pkt_capture_callback(void *soc, enum WDI_EVENT event, void *log_data,
			  u_int16_t vdev_id, uint32_t status)
{
	uint8_t bssid[QDF_MAC_ADDR_SIZE];
	uint8_t tid = 0, tx_retry_cnt = 0;
	struct htt_tx_data_hdr_information cmpl_desc;
	struct htt_tx_data_hdr_information *ptr_cmpl_desc;
	struct hal_tx_completion_status ppdu_hdr = {0};
	uint32_t txcap_hdr_size = sizeof(struct htt_tx_data_hdr_information);
	struct dp_soc *psoc = soc;

	switch (event) {
	case WDI_EVENT_PKT_CAPTURE_TX_DATA:
	{
		struct dp_tx_desc_s *desc = log_data;
		qdf_nbuf_t netbuf;
		int nbuf_len;

		hal_tx_comp_get_status(&desc->comp, &ppdu_hdr, psoc->hal_soc);
		if (!(pkt_capture_get_pktcap_mode_lithium() &
					PKT_CAPTURE_MODE_DATA_ONLY)) {
			return;
		}

		cmpl_desc.phy_timestamp_l32 = ppdu_hdr.tsf;
		cmpl_desc.preamble = ppdu_hdr.pkt_type;
		cmpl_desc.mcs = ppdu_hdr.mcs;
		cmpl_desc.bw = ppdu_hdr.bw;
		/* nss is not updated */
		cmpl_desc.nss = 0;
		cmpl_desc.rssi = ppdu_hdr.ack_frame_rssi;
		/* rate is not updated */
		cmpl_desc.rate = 0;
		cmpl_desc.stbc = ppdu_hdr.stbc;
		cmpl_desc.sgi = ppdu_hdr.sgi;
		cmpl_desc.ldpc = ppdu_hdr.ldpc;
		/* beamformed is not updated */
		cmpl_desc.beamformed = 0;
		cmpl_desc.framectrl = 0x0008;
		cmpl_desc.tx_retry_cnt = ppdu_hdr.transmit_cnt;
		tid = ppdu_hdr.tid;
		status = ppdu_hdr.status;
		tx_retry_cnt = ppdu_hdr.transmit_cnt;

		nbuf_len = qdf_nbuf_len(desc->nbuf);
		netbuf = qdf_nbuf_alloc(NULL,
					roundup(nbuf_len + RESERVE_BYTES, 4),
					RESERVE_BYTES, 4, false);

		if (!netbuf)
			return;

		qdf_nbuf_put_tail(netbuf, nbuf_len);

		qdf_mem_copy(qdf_nbuf_data(netbuf),
			     qdf_nbuf_data(desc->nbuf), nbuf_len);

		if (qdf_unlikely(qdf_nbuf_headroom(netbuf) < txcap_hdr_size)) {
			netbuf = qdf_nbuf_realloc_headroom(netbuf,
							   txcap_hdr_size);
			if (!netbuf) {
				QDF_TRACE(QDF_MODULE_ID_PKT_CAPTURE,
					  QDF_TRACE_LEVEL_ERROR,
					  FL("No headroom"));
				return;
			}
		}

		if (!qdf_nbuf_push_head(netbuf, txcap_hdr_size)) {
			QDF_TRACE(QDF_MODULE_ID_PKT_CAPTURE,
				  QDF_TRACE_LEVEL_ERROR, FL("No headroom"));
			qdf_nbuf_free(netbuf);
			return;
		}

		ptr_cmpl_desc =
		(struct htt_tx_data_hdr_information *)qdf_nbuf_data(netbuf);
		qdf_mem_copy(ptr_cmpl_desc, &cmpl_desc, txcap_hdr_size);

		pkt_capture_datapkt_process(
			vdev_id, netbuf, TXRX_PROCESS_TYPE_DATA_TX_COMPL,
			tid, status, TXRX_PKTCAPTURE_PKT_FORMAT_8023,
			bssid, NULL, tx_retry_cnt);

		break;
	}

	case WDI_EVENT_PKT_CAPTURE_RX_DATA:
	{
		if (!(pkt_capture_get_pktcap_mode_lithium() &
					PKT_CAPTURE_MODE_DATA_ONLY))
			return;

		pkt_capture_msdu_process_pkts(bssid, log_data, vdev_id, soc);
		break;
	}

	case WDI_EVENT_PKT_CAPTURE_OFFLOAD_TX_DATA:
	{
		struct htt_tx_offload_deliver_ind_hdr_t *offload_deliver_msg;
		bool is_pkt_during_roam = false;
		uint32_t freq = 0;

		if (!(pkt_capture_get_pktcap_mode_lithium() &
					PKT_CAPTURE_MODE_DATA_ONLY))
			return;

		offload_deliver_msg =
		(struct htt_tx_offload_deliver_ind_hdr_t *)log_data;
		is_pkt_during_roam =
		(offload_deliver_msg->reserved_2 ? true : false);

		if (is_pkt_during_roam) {
			vdev_id = HTT_INVALID_VDEV;
			freq =
			(uint32_t)offload_deliver_msg->reserved_3;
		} else {
			vdev_id = offload_deliver_msg->vdev_id;
		}

		pkt_capture_offload_deliver_indication_handler(
						log_data,
						vdev_id, bssid, soc);
	}

	default:
		break;
	}
}

#else
static void pkt_capture_wdi_event_subscribe(struct wlan_objmgr_psoc *psoc)
{
}

static void pkt_capture_wdi_event_unsubscribe(struct wlan_objmgr_psoc *psoc)
{
}
#endif

struct wlan_objmgr_vdev *pkt_capture_get_vdev()
{
	return gp_pkt_capture_vdev;
}

enum pkt_capture_mode pkt_capture_get_mode(struct wlan_objmgr_psoc *psoc)
{
	struct pkt_psoc_priv *psoc_priv;

	if (!psoc) {
		pkt_capture_err("psoc is NULL");
		return PACKET_CAPTURE_MODE_DISABLE;
	}

	psoc_priv = pkt_capture_psoc_get_priv(psoc);
	if (!psoc_priv) {
		pkt_capture_err("psoc_priv is NULL");
		return PACKET_CAPTURE_MODE_DISABLE;
	}

	return psoc_priv->cfg_param.pkt_capture_mode;
}

QDF_STATUS
pkt_capture_register_callbacks(struct wlan_objmgr_vdev *vdev,
			       QDF_STATUS (*mon_cb)(void *, qdf_nbuf_t),
			       void *context)
{
	struct pkt_capture_vdev_priv *vdev_priv;
	struct wlan_objmgr_psoc *psoc;
	enum pkt_capture_mode mode;
	QDF_STATUS status;

	if (!vdev) {
		pkt_capture_err("vdev is NULL");
		return QDF_STATUS_E_INVAL;
	}

	psoc = wlan_vdev_get_psoc(vdev);
	if (!psoc) {
		pkt_capture_err("psoc is NULL");
		return QDF_STATUS_E_INVAL;
	}

	vdev_priv = pkt_capture_vdev_get_priv(vdev);
	if (!vdev_priv) {
		pkt_capture_err("vdev priv is NULL");
		return QDF_STATUS_E_INVAL;
	}

	vdev_priv->cb_ctx->mon_cb = mon_cb;
	vdev_priv->cb_ctx->mon_ctx = context;

	status = pkt_capture_mgmt_rx_ops(psoc, true);
	if (QDF_IS_STATUS_ERROR(status)) {
		pkt_capture_err("Failed to register pkt capture mgmt rx ops");
		goto mgmt_rx_ops_fail;
	}

	target_if_pkt_capture_register_tx_ops(&vdev_priv->tx_ops);
	target_if_pkt_capture_register_rx_ops(&vdev_priv->rx_ops);
	pkt_capture_wdi_event_subscribe(psoc);
	pkt_capture_record_channel(vdev);

	status = tgt_pkt_capture_register_ev_handler(vdev);
	if (QDF_IS_STATUS_ERROR(status))
		goto register_ev_handlers_fail;

	/*
	 * set register event bit so that mon thread will start
	 * processing packets in queue.
	 */
	set_bit(PKT_CAPTURE_REGISTER_EVENT,
		&vdev_priv->mon_ctx->mon_event_flag);

	mode = pkt_capture_get_mode(psoc);
	status = tgt_pkt_capture_send_mode(vdev, mode);
	if (QDF_IS_STATUS_ERROR(status)) {
		pkt_capture_err("Unable to send packet capture mode to fw");
		goto send_mode_fail;
	}

	return QDF_STATUS_SUCCESS;

send_mode_fail:
	tgt_pkt_capture_unregister_ev_handler(vdev);
register_ev_handlers_fail:
	pkt_capture_mgmt_rx_ops(psoc, false);
mgmt_rx_ops_fail:
	vdev_priv->cb_ctx->mon_cb = NULL;
	vdev_priv->cb_ctx->mon_ctx = NULL;

	return status;
}

QDF_STATUS pkt_capture_deregister_callbacks(struct wlan_objmgr_vdev *vdev)
{
	struct pkt_capture_vdev_priv *vdev_priv;
	struct wlan_objmgr_psoc *psoc;
	QDF_STATUS status;

	if (!vdev) {
		pkt_capture_err("vdev is NULL");
		return QDF_STATUS_E_INVAL;
	}

	psoc = wlan_vdev_get_psoc(vdev);
	if (!psoc) {
		pkt_capture_err("psoc is NULL");
		return QDF_STATUS_E_INVAL;
	}

	vdev_priv = pkt_capture_vdev_get_priv(vdev);
	if (!vdev_priv) {
		pkt_capture_err("vdev priv is NULL");
		return QDF_STATUS_E_INVAL;
	}

	status = tgt_pkt_capture_send_mode(vdev, PACKET_CAPTURE_MODE_DISABLE);
	if (QDF_IS_STATUS_ERROR(status))
		pkt_capture_err("Unable to send packet capture mode to fw");

	/*
	 * Clear packet capture register event so that mon thread will
	 * stop processing packets in queue.
	 */
	clear_bit(PKT_CAPTURE_REGISTER_EVENT,
		  &vdev_priv->mon_ctx->mon_event_flag);
	set_bit(PKT_CAPTURE_RX_POST_EVENT,
		&vdev_priv->mon_ctx->mon_event_flag);
	wake_up_interruptible(&vdev_priv->mon_ctx->mon_wait_queue);

	/*
	 * Wait till current packet process completes in mon thread and
	 * flush the remaining packet in queue.
	 */
	wait_for_completion(&vdev_priv->mon_ctx->mon_register_event);
	pkt_capture_drop_monpkt(vdev_priv->mon_ctx);

	pkt_capture_wdi_event_unsubscribe(psoc);
	status = tgt_pkt_capture_unregister_ev_handler(vdev);
	if (QDF_IS_STATUS_ERROR(status))
		pkt_capture_err("Unable to unregister event handlers");

	status = pkt_capture_mgmt_rx_ops(psoc, false);
	if (QDF_IS_STATUS_ERROR(status))
		pkt_capture_err("Failed to unregister pkt capture mgmt rx ops");

	vdev_priv->cb_ctx->mon_cb = NULL;
	vdev_priv->cb_ctx->mon_ctx = NULL;

	return QDF_STATUS_SUCCESS;
}

void pkt_capture_set_pktcap_mode(struct wlan_objmgr_psoc *psoc,
				 enum pkt_capture_mode mode)
{
	struct pkt_capture_vdev_priv *vdev_priv;
	struct wlan_objmgr_vdev *vdev;

	if (!psoc) {
		pkt_capture_err("psoc is NULL");
		return;
	}

	vdev = wlan_objmgr_get_vdev_by_opmode_from_psoc(psoc,
							QDF_STA_MODE,
							WLAN_PKT_CAPTURE_ID);
	if (!vdev) {
		pkt_capture_err("vdev is NULL");
		return;
	}

	vdev_priv = pkt_capture_vdev_get_priv(vdev);
	if (vdev_priv)
		vdev_priv->cb_ctx->pkt_capture_mode = mode;
	else
		pkt_capture_err("vdev_priv is NULL");

	wlan_objmgr_vdev_release_ref(vdev, WLAN_PKT_CAPTURE_ID);
}

enum pkt_capture_mode
pkt_capture_get_pktcap_mode(struct wlan_objmgr_psoc *psoc)
{
	enum pkt_capture_mode mode = PACKET_CAPTURE_MODE_DISABLE;
	struct pkt_capture_vdev_priv *vdev_priv;
	struct wlan_objmgr_vdev *vdev;

	if (!psoc) {
		pkt_capture_err("psoc is NULL");
		return 0;
	}

	if (!pkt_capture_get_mode(psoc))
		return 0;

	vdev = wlan_objmgr_get_vdev_by_opmode_from_psoc(psoc,
							QDF_STA_MODE,
							WLAN_PKT_CAPTURE_ID);
	if (!vdev)
		return 0;

	vdev_priv = pkt_capture_vdev_get_priv(vdev);
	if (!vdev_priv)
		pkt_capture_err("vdev_priv is NULL");
	else
		mode = vdev_priv->cb_ctx->pkt_capture_mode;

	wlan_objmgr_vdev_release_ref(vdev, WLAN_PKT_CAPTURE_ID);
	return mode;
}

/**
 * pkt_capture_callback_ctx_create() - Create packet capture callback context
 * @vdev_priv: pointer to packet capture vdev priv obj
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS
pkt_capture_callback_ctx_create(struct pkt_capture_vdev_priv *vdev_priv)
{
	struct pkt_capture_cb_context *cb_ctx;

	cb_ctx = qdf_mem_malloc(sizeof(*cb_ctx));
	if (!cb_ctx) {
		pkt_capture_err("MON context create failed");
		return QDF_STATUS_E_NOMEM;
	}

	vdev_priv->cb_ctx = cb_ctx;

	return QDF_STATUS_SUCCESS;
}

/**
 * pkt_capture_callback_ctx_destroy() - Destroy packet capture callback context
 * @vdev_priv: pointer to packet capture vdev priv obj
 *
 * Return: None
 */
static void
pkt_capture_callback_ctx_destroy(struct pkt_capture_vdev_priv *vdev_priv)
{
	qdf_mem_free(vdev_priv->cb_ctx);
}

/**
 * pkt_capture_mon_context_create() - Create packet capture mon context
 * @vdev_priv: pointer to packet capture vdev priv obj
 *
 * This function allocates memory for packet capture mon context
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS
pkt_capture_mon_context_create(struct pkt_capture_vdev_priv *vdev_priv)
{
	struct pkt_capture_mon_context *mon_context;

	mon_context = qdf_mem_malloc(sizeof(*mon_context));
	if (!mon_context) {
		pkt_capture_err("MON context create failed");
		return QDF_STATUS_E_NOMEM;
	}

	vdev_priv->mon_ctx = mon_context;

	return QDF_STATUS_SUCCESS;
}

/**
 * pkt_capture_mon_context_destroy() - Destroy packet capture mon context
 * @vdev_priv: pointer to packet capture vdev priv obj
 *
 * Free packet capture mon context
 *
 * Return: None
 */
static void
pkt_capture_mon_context_destroy(struct pkt_capture_vdev_priv *vdev_priv)
{
	qdf_mem_free(vdev_priv->mon_ctx);
}

uint32_t pkt_capture_drop_nbuf_list(qdf_nbuf_t buf_list)
{
	qdf_nbuf_t buf, next_buf;
	uint32_t num_dropped = 0;

	buf = buf_list;
	while (buf) {
		QDF_NBUF_CB_RX_PEER_CACHED_FRM(buf) = 1;
		next_buf = qdf_nbuf_queue_next(buf);
		qdf_nbuf_free(buf);
		buf = next_buf;
		num_dropped++;
	}
	return num_dropped;
}

/**
 * pkt_capture_cfg_init() - Initialize packet capture cfg ini params
 * @psoc_priv: psoc private object
 *
 * Return: None
 */
static void
pkt_capture_cfg_init(struct pkt_psoc_priv *psoc_priv)
{
	struct pkt_capture_cfg *cfg_param;

	cfg_param = &psoc_priv->cfg_param;

	cfg_param->pkt_capture_mode = cfg_get(psoc_priv->psoc,
					      CFG_PKT_CAPTURE_MODE);
}

QDF_STATUS
pkt_capture_vdev_create_notification(struct wlan_objmgr_vdev *vdev, void *arg)
{
	struct pkt_capture_mon_context *mon_ctx;
	struct pkt_capture_vdev_priv *vdev_priv;
	QDF_STATUS status;

	if ((wlan_vdev_mlme_get_opmode(vdev) != QDF_STA_MODE) ||
	    !pkt_capture_get_mode(wlan_vdev_get_psoc(vdev)))
		return QDF_STATUS_SUCCESS;

	vdev_priv = qdf_mem_malloc(sizeof(*vdev_priv));
	if (!vdev_priv)
		return QDF_STATUS_E_NOMEM;

	status = wlan_objmgr_vdev_component_obj_attach(
					vdev,
					WLAN_UMAC_COMP_PKT_CAPTURE,
					vdev_priv, QDF_STATUS_SUCCESS);
	if (QDF_IS_STATUS_ERROR(status)) {
		pkt_capture_err("Failed to attach vdev component obj");
		goto free_vdev_priv;
	}

	vdev_priv->vdev = vdev;
	gp_pkt_capture_vdev = vdev;

	status = pkt_capture_callback_ctx_create(vdev_priv);
	if (!QDF_IS_STATUS_SUCCESS(status)) {
		pkt_capture_err("Failed to create callback context");
		goto detach_vdev_priv;
	}

	status = pkt_capture_mon_context_create(vdev_priv);
	if (QDF_IS_STATUS_ERROR(status)) {
		pkt_capture_err("Failed to create mon context");
		goto destroy_pkt_capture_cb_context;
	}

	mon_ctx = vdev_priv->mon_ctx;

	status = pkt_capture_alloc_mon_thread(mon_ctx);
	if (QDF_IS_STATUS_ERROR(status)) {
		pkt_capture_err("Failed to alloc mon thread");
		goto destroy_mon_context;
	}

	status = pkt_capture_open_mon_thread(mon_ctx);
	if (QDF_IS_STATUS_ERROR(status)) {
		pkt_capture_err("Failed to open mon thread");
		goto open_mon_thread_fail;
	}
	return status;

open_mon_thread_fail:
	pkt_capture_free_mon_pkt_freeq(mon_ctx);
destroy_mon_context:
	pkt_capture_mon_context_destroy(vdev_priv);
destroy_pkt_capture_cb_context:
	pkt_capture_callback_ctx_destroy(vdev_priv);
detach_vdev_priv:
	wlan_objmgr_vdev_component_obj_detach(vdev,
					      WLAN_UMAC_COMP_PKT_CAPTURE,
					      vdev_priv);
free_vdev_priv:
	qdf_mem_free(vdev_priv);
	return status;
}

QDF_STATUS
pkt_capture_vdev_destroy_notification(struct wlan_objmgr_vdev *vdev, void *arg)
{
	struct pkt_capture_vdev_priv *vdev_priv;
	QDF_STATUS status;

	if ((wlan_vdev_mlme_get_opmode(vdev) != QDF_STA_MODE) ||
	    !pkt_capture_get_mode(wlan_vdev_get_psoc(vdev)))
		return QDF_STATUS_SUCCESS;

	vdev_priv = pkt_capture_vdev_get_priv(vdev);
	if (!vdev_priv) {
		pkt_capture_err("vdev priv is NULL");
		return QDF_STATUS_E_FAILURE;
	}

	status = wlan_objmgr_vdev_component_obj_detach(
					vdev,
					WLAN_UMAC_COMP_PKT_CAPTURE,
					vdev_priv);
	if (QDF_IS_STATUS_ERROR(status))
		pkt_capture_err("Failed to detach vdev component obj");

	pkt_capture_close_mon_thread(vdev_priv->mon_ctx);
	pkt_capture_mon_context_destroy(vdev_priv);
	pkt_capture_callback_ctx_destroy(vdev_priv);
	qdf_mem_free(vdev_priv);
	gp_pkt_capture_vdev = NULL;
	return status;
}

QDF_STATUS
pkt_capture_psoc_create_notification(struct wlan_objmgr_psoc *psoc, void *arg)
{
	struct pkt_psoc_priv *psoc_priv;
	QDF_STATUS status;

	psoc_priv = qdf_mem_malloc(sizeof(*psoc_priv));
	if (!psoc_priv)
		return QDF_STATUS_E_NOMEM;

	status = wlan_objmgr_psoc_component_obj_attach(psoc,
				WLAN_UMAC_COMP_PKT_CAPTURE,
				psoc_priv, QDF_STATUS_SUCCESS);
	if (QDF_IS_STATUS_ERROR(status)) {
		pkt_capture_err("Failed to attach psoc component obj");
		goto free_psoc_priv;
	}

	psoc_priv->psoc = psoc;
	pkt_capture_cfg_init(psoc_priv);

	return status;

free_psoc_priv:
	qdf_mem_free(psoc_priv);
	return status;
}

QDF_STATUS
pkt_capture_psoc_destroy_notification(struct wlan_objmgr_psoc *psoc, void *arg)
{
	struct pkt_psoc_priv *psoc_priv;
	QDF_STATUS status;

	psoc_priv = pkt_capture_psoc_get_priv(psoc);
	if (!psoc_priv) {
		pkt_capture_err("psoc priv is NULL");
		return QDF_STATUS_E_FAILURE;
	}

	status = wlan_objmgr_psoc_component_obj_detach(psoc,
					WLAN_UMAC_COMP_PKT_CAPTURE,
					psoc_priv);
	if (QDF_IS_STATUS_ERROR(status)) {
		pkt_capture_err("Failed to detach psoc component obj");
		return status;
	}

	qdf_mem_free(psoc_priv);
	return status;
}

void pkt_capture_record_channel(struct wlan_objmgr_vdev *vdev)
{
	struct wlan_objmgr_pdev *pdev = wlan_vdev_get_pdev(vdev);
	void *soc = cds_get_context(QDF_MODULE_ID_SOC);
	struct wlan_channel *des_chan;
	cdp_config_param_type val;
	struct wlan_objmgr_psoc *psoc;

	psoc = wlan_vdev_get_psoc(vdev);

	if (!pkt_capture_get_mode(psoc))
		return;
	/*
	 * Record packet capture channel here
	 */
	des_chan = vdev->vdev_mlme.des_chan;
	val.cdp_pdev_param_monitor_chan = des_chan->ch_ieee;
	cdp_txrx_set_pdev_param(soc, wlan_objmgr_pdev_get_pdev_id(pdev),
				CDP_MONITOR_CHANNEL, val);
	val.cdp_pdev_param_mon_freq = des_chan->ch_freq;
	cdp_txrx_set_pdev_param(soc, wlan_objmgr_pdev_get_pdev_id(pdev),
				CDP_MONITOR_FREQUENCY, val);
}
