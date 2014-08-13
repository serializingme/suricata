/* Copyright (C) 2014 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/**
 * \file
 *
 * \author Ignacio Sanchez <sanchezmartin.ji@gmail.com>
 * \author Duarte Silva <duarte.silva@serializing.me>
 */

#include "suricata-common.h"
#include "conf.h"

#include "util-misc.h"

#include "app-layer-parser.h"
#include "app-layer-htp.h"
#include "app-layer-htp-xff.h"

/** XFF header value minimal length */
#define XFF_CHAIN_MINLEN 7
/** XFF header value maximum length */
#define XFF_CHAIN_MAXLEN 256
/** Default XFF header name */
#define XFF_DEFAULT "X-Forwarded-For"

int GetXFFIPFromTx(const Packet *p, uint64_t tx_id, char *xff_header, char *dstbuf,
        int dstbuflen)
{
    uint8_t xff_chain[XFF_CHAIN_MAXLEN];
    HtpState *htp_state = NULL;
    htp_tx_t *tx = NULL;
    uint64_t total_txs = 0;

    htp_state = (HtpState *)FlowGetAppState(p->flow);

    if (htp_state == NULL) {
        SCLogDebug("no http state, XFF IP cannot be retrieved");
        return 0;
    }

    total_txs = AppLayerParserGetTxCnt(p->flow->proto, ALPROTO_HTTP, htp_state);
    if (tx_id >= total_txs)
        return 0;

    tx = AppLayerParserGetTx(p->flow->proto, ALPROTO_HTTP, htp_state, tx_id);
    if (tx == NULL) {
        SCLogDebug("tx is NULL, XFF cannot be retrieved");
        return 0;
    }

    htp_header_t *h_xff = NULL;
    if (tx->request_headers != NULL) {
        h_xff = htp_table_get_c(tx->request_headers, xff_header);
    }

    if (h_xff != NULL && bstr_len(h_xff->value) >= XFF_CHAIN_MINLEN &&
            bstr_len(h_xff->value) < XFF_CHAIN_MAXLEN) {

        memcpy(xff_chain, bstr_ptr(h_xff->value), bstr_len(h_xff->value));
        xff_chain[bstr_len(h_xff->value)]=0;
        /** Check for chained IP's separated by ", ", we will get the last one */
        uint8_t *p_xff = memrchr(xff_chain, ' ', bstr_len(h_xff->value));
        if (p_xff == NULL) {
            p_xff = xff_chain;
        } else {
            p_xff++;
        }
        /** Sanity check on extracted IP for IPv4 and IPv6 */
        uint32_t ip[4];
        if ( inet_pton(AF_INET, (char *)p_xff, ip ) == 1 ||
                inet_pton(AF_INET6, (char *)p_xff, ip ) == 1 ) {
            strlcpy(dstbuf, (char *)p_xff, dstbuflen);
            return 1; // OK
        }
    }
    return 0;
}

/**
 *  \brief Function to return XFF IP if any...
 *  \retval 1 if the IP has been found and returned in dstbuf
 *  \retval 0 if the IP has not being found or error
 */
int GetXFFIP(const Packet *p, char *xff_header, char *dstbuf, int dstbuflen)
{
    HtpState *htp_state = NULL;
    uint64_t tx_id = 0;
    uint64_t total_txs = 0;

    htp_state = (HtpState *)FlowGetAppState(p->flow);
    if (htp_state == NULL) {
        SCLogDebug("no http state, XFF IP cannot be retrieved");
        goto end;
    }

    total_txs = AppLayerParserGetTxCnt(p->flow->proto, ALPROTO_HTTP, htp_state);
    for (; tx_id < total_txs; tx_id++) {
        if (GetXFFIPFromTx(p, tx_id, xff_header, dstbuf, dstbuflen) == 1)
            return 1;
    }

end:
    return 0; // Not found
}

void GetXFFCfg(ConfNode *conf, XFFCfg *result)
{
    BUG_ON(conf == NULL || result == NULL);

    ConfNode *xff_node = NULL;

    if (conf != NULL)
        xff_node = ConfNodeLookupChild(conf, "xff");

    if (xff_node != NULL && ConfNodeChildValueIsTrue(xff_node, "enabled")) {
        const char *xff_mode = ConfNodeLookupChildValue(xff_node, "mode");

        if (xff_mode != NULL && strcasecmp(xff_mode, "overwrite") == 0) {
            result->mode |= XFF_OVERWRITE;
        } else {
            if (xff_mode == NULL) {
                SCLogWarning(SC_WARN_XFF_INVALID_MODE, "The XFF mode hasn't been defined, falling back to extra-data mode");
            }
            else if (strcasecmp(xff_mode, "extra-data") != 0) {
                SCLogWarning(SC_WARN_XFF_INVALID_MODE, "The XFF mode %s is invalid, falling back to extra-data mode",
                        xff_mode);
            }
            result->mode |= XFF_EXTRADATA;
        }

        const char *xff_header = ConfNodeLookupChildValue(xff_node, "header");

        if (xff_header != NULL) {
            result->header = (char *) xff_header;
        } else {
            SCLogWarning(SC_WARN_XFF_INVALID_HEADER, "The XFF header hasn't been defined, using the default %s",
                    XFF_DEFAULT);
            result->header = XFF_DEFAULT;
        }
    }
    else {
        result->mode = XFF_DISABLED;
    }
}
