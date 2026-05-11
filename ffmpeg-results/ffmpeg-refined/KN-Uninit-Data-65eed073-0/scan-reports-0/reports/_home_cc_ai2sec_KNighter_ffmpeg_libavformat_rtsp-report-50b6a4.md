### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/rtsp.c  
---|---  
Warning:| line 2116, column 37  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


2048  |                 }
2049  |  if (fdsnum != 2) {
2050  |                     av_log(s, AV_LOG_ERROR,
2051  |  "Number of fds %d not supported\n", fdsnum);
2052  |  return AVERROR_INVALIDDATA;
2053  |                 }
2054  |  for (fdsidx = 0; fdsidx < fdsnum; fdsidx++) {
2055  |                     p[rt->max_p].fd       = fds[fdsidx];
2056  |                     p[rt->max_p++].events = POLLIN;
2057  |                 }
2058  |                 av_freep(&fds);
2059  |             }
2060  |         }
2061  |     }
2062  |  
2063  |  for (;;) {
2064  |  if (ff_check_interrupt(&s->interrupt_callback))
2065  |  return AVERROR_EXIT;
2066  |  if (wait_end && wait_end - av_gettime_relative() < 0)
2067  |  return AVERROR(EAGAIN);
2068  |         n = poll(p, rt->max_p, POLLING_TIME);
2069  |  if (n > 0) {
2070  |  int j = rt->rtsp_hd ? 1 : 0;
2071  |  for (i = 0; i < rt->nb_rtsp_streams; i++) {
2072  |                 rtsp_st = rt->rtsp_streams[i];
2073  |  if (rtsp_st->rtp_handle) {
2074  |  if (p[j].revents & POLLIN || p[j+1].revents & POLLIN) {
2075  |                         ret = ffurl_read(rtsp_st->rtp_handle, buf, buf_size);
2076  |  if (ret > 0) {
2077  |                             *prtsp_st = rtsp_st;
2078  |  return ret;
2079  |                         }
2080  |                     }
2081  |                     j+=2;
2082  |                 }
2083  |             }
2084  | #if CONFIG_RTSP_DEMUXER
2085  |  if (rt->rtsp_hd && p[0].revents & POLLIN) {
2086  |  if ((ret = parse_rtsp_message(s)) < 0) {
2087  |  return ret;
2088  |                 }
2089  |             }
2090  | #endif
2091  |         } else if (n == 0 && rt->initial_timeout > 0 && --runs <= 0) {
2092  |  return AVERROR(ETIMEDOUT);
2093  |         } else if (n < 0 && errno != EINTR)
2094  |  return AVERROR(errno);
2095  |     }
2096  | }
2097  |  
2098  | static int pick_stream(AVFormatContext *s, RTSPStream **rtsp_st,
2099  |  const uint8_t *buf, int len)
2100  | {
2101  |  RTSPState *rt = s->priv_data;
2102  |  int i;
2103  |  if (len < 0)
    25←Assuming 'len' is >= 0→
    26←Taking false branch→
2104  |  return len;
2105  |  if (rt->nb_rtsp_streams == 1) {
    27←Assuming field 'nb_rtsp_streams' is not equal to 1→
2106  |         *rtsp_st = rt->rtsp_streams[0];
2107  |  return len;
2108  |     }
2109  |  if (len >= 8 && rt->transport == RTSP_TRANSPORT_RTP) {
    28←Assuming 'len' is >= 8→
    29←Assuming field 'transport' is equal to RTSP_TRANSPORT_RTP→
2110  |  if (RTP_PT_IS_RTCP(rt->recvbuf[1])) {
    30←Taking true branch→
    31←Assuming the condition is true→
    32←Assuming the condition is true→
2111  |  int no_ssrc = 0;
2112  |  for (i = 0; i < rt->nb_rtsp_streams; i++) {
    33←Assuming 'i' is < field 'nb_rtsp_streams'→
    34←Loop condition is true.  Entering loop body→
2113  |  RTPDemuxContext *rtpctx = rt->rtsp_streams[i]->transport_priv;
2114  |  if (!rtpctx)
    35←Assuming 'rtpctx' is non-null→
    36←Taking false branch→
2115  |  continue;
2116  |  if (rtpctx->ssrc == AV_RB32(&buf[4])) {
    37←buffer read by avio_read may be partially uninitialized
2117  |                     *rtsp_st = rt->rtsp_streams[i];
2118  |  return len;
2119  |                 }
2120  |  if (!rtpctx->ssrc)
2121  |                     no_ssrc = 1;
2122  |             }
2123  |  if (no_ssrc) {
2124  |                 av_log(s, AV_LOG_WARNING,
2125  |  "Unable to pick stream for packet - SSRC not known for "
2126  |  "all streams\n");
2127  |  return AVERROR(EAGAIN);
2128  |             }
2129  |         } else {
2130  |  for (i = 0; i < rt->nb_rtsp_streams; i++) {
2131  |  if ((buf[1] & 0x7f) == rt->rtsp_streams[i]->sdp_payload_type) {
2132  |                     *rtsp_st = rt->rtsp_streams[i];
2133  |  return len;
2134  |                 }
2135  |             }
2136  |         }
2137  |     }
2138  |     av_log(s, AV_LOG_WARNING, "Unable to pick stream for packet\n");
2139  |  return AVERROR(EAGAIN);
2140  | }
2141  |  
2142  | static int read_packet(AVFormatContext *s,
2143  |                        RTSPStream **rtsp_st, RTSPStream *first_queue_st,
2144  |                        int64_t wait_end)
2145  | {
2146  |  RTSPState *rt = s->priv_data;
2147  |  int len;
2148  |  
2149  |  switch(rt->lower_transport) {
    23←Control jumps to 'case RTSP_LOWER_TRANSPORT_CUSTOM:'  at line 2162→
2150  |  default:
2151  | #if CONFIG_RTSP_DEMUXER
2152  |  case RTSP_LOWER_TRANSPORT_TCP:
2153  |         len = ff_rtsp_tcp_read_packet(s, rtsp_st, rt->recvbuf, RECVBUF_SIZE);
2154  |  break;
2155  | #endif
2156  |  case RTSP_LOWER_TRANSPORT_UDP:
2157  |  case RTSP_LOWER_TRANSPORT_UDP_MULTICAST:
2158  |         len = udp_read_packet(s, rtsp_st, rt->recvbuf, RECVBUF_SIZE, wait_end);
2159  |  if (len > 0 && (*rtsp_st)->transport_priv && rt->transport == RTSP_TRANSPORT_RTP)
2160  |             ff_rtp_check_and_send_back_rr((*rtsp_st)->transport_priv, (*rtsp_st)->rtp_handle, NULL, len);
2161  |  break;
2162  |  case RTSP_LOWER_TRANSPORT_CUSTOM:
2163  |  if (first_queue_st23.1'first_queue_st' is null && rt->transport == RTSP_TRANSPORT_RTP &&
2164  |             wait_end && wait_end < av_gettime_relative())
2165  |             len = AVERROR(EAGAIN);
2166  |  else
2167  |  len = avio_read_partial(s->pb, rt->recvbuf, RECVBUF_SIZE);
2168  |  len = pick_stream(s, rtsp_st, rt->recvbuf, len);
    24←Calling 'pick_stream'→
2169  |  if (len > 0 && (*rtsp_st)->transport_priv && rt->transport == RTSP_TRANSPORT_RTP)
2170  |             ff_rtp_check_and_send_back_rr((*rtsp_st)->transport_priv, NULL, s->pb, len);
2171  |  break;
2172  |     }
2173  |  
2174  |  if (len == 0)
2175  |  return AVERROR_EOF;
2176  |  
2177  |  return len;
2178  | }
2179  |  
2180  | int ff_rtsp_fetch_packet(AVFormatContext *s, AVPacket *pkt)
2181  | {
2182  |  RTSPState *rt = s->priv_data;
2183  |  int ret, len;
2184  |     RTSPStream *rtsp_st, *first_queue_st = NULL;
2185  |     int64_t wait_end = 0;
2186  |  
2187  |  if (rt->nb_byes == rt->nb_rtsp_streams)
    1Assuming field 'nb_byes' is not equal to field 'nb_rtsp_streams'→
    2←Taking false branch→
2188  |  return AVERROR_EOF;
2189  |  
2190  |  /* get next frames from the same RTP packet */
2191  |  if (rt->cur_transport_priv) {
    3←Assuming field 'cur_transport_priv' is null→
    4←Taking false branch→
2192  |  if (rt->transport == RTSP_TRANSPORT_RDT) {
2193  |             ret = ff_rdt_parse_packet(rt->cur_transport_priv, pkt, NULL, 0);
2194  |         } else if (rt->transport == RTSP_TRANSPORT_RTP) {
2195  |             ret = ff_rtp_parse_packet(rt->cur_transport_priv, pkt, NULL, 0);
2196  |         } else if (CONFIG_RTPDEC && rt->ts) {
2197  |             ret = avpriv_mpegts_parse_packet(rt->ts, pkt, rt->recvbuf + rt->recvbuf_pos, rt->recvbuf_len - rt->recvbuf_pos);
2198  |  if (ret >= 0) {
2199  |                 rt->recvbuf_pos += ret;
2200  |                 ret = rt->recvbuf_pos < rt->recvbuf_len;
2201  |             }
2202  |         } else
2203  |             ret = -1;
2204  |  if (ret == 0) {
2205  |             rt->cur_transport_priv = NULL;
2206  |  return 0;
2207  |         } else if (ret == 1) {
2208  |  return 0;
2209  |         } else
2210  |             rt->cur_transport_priv = NULL;
2211  |     }
2212  |  
2213  | redo:
2214  |  if (rt->transport19.1Field 'transport' is not equal to RTSP_TRANSPORT_RTP == RTSP_TRANSPORT_RTP) {
    5←Assuming field 'transport' is not equal to RTSP_TRANSPORT_RTP→
    6←Taking false branch→
    20←Taking false branch→
2215  |  int i;
2216  |         int64_t first_queue_time = 0;
2217  |  for (i = 0; i < rt->nb_rtsp_streams; i++) {
2218  |             RTPDemuxContext *rtpctx = rt->rtsp_streams[i]->transport_priv;
2219  |             int64_t queue_time;
2220  |  if (!rtpctx)
2221  |  continue;
2222  |             queue_time = ff_rtp_queued_packet_time(rtpctx);
2223  |  if (queue_time && (queue_time - first_queue_time < 0 ||
2224  |                                !first_queue_time)) {
2225  |                 first_queue_time = queue_time;
2226  |                 first_queue_st   = rt->rtsp_streams[i];
2227  |             }
2228  |         }
2229  |  if (first_queue_time) {
2230  |             wait_end = first_queue_time + s->max_delay;
2231  |         } else {
2232  |             wait_end = 0;
2233  |             first_queue_st = NULL;
2234  |         }
2235  |     }
2236  |  
2237  |  /* read next RTP packet */
2238  |  if (!rt->recvbuf20.1Field 'recvbuf' is non-null) {
    7←Assuming field 'recvbuf' is non-null→
    8←Taking false branch→
    21←Taking false branch→
2239  |         rt->recvbuf = av_malloc(RECVBUF_SIZE);
2240  |  if (!rt->recvbuf)
2241  |  return AVERROR(ENOMEM);
2242  |     }
2243  |  
2244  |  len = read_packet(s, &rtsp_st, first_queue_st, wait_end);
    22←Calling 'read_packet'→
2245  |  if (len == AVERROR(EAGAIN) && first_queue_st &&
    9←Assuming the condition is false→
2246  |         rt->transport == RTSP_TRANSPORT_RTP) {
2247  |         av_log(s, AV_LOG_WARNING,
2248  |  "max delay reached. need to consume packet\n");
2249  |         rtsp_st = first_queue_st;
2250  |         ret = ff_rtp_parse_packet(rtsp_st->transport_priv, pkt, NULL, 0);
2251  |  goto end;
2252  |     }
2253  |  if (len < 0)
    10←Assuming 'len' is >= 0→
    11←Taking false branch→
2254  |  return len;
2255  |  
2256  |  if (rt->transport == RTSP_TRANSPORT_RDT) {
    12←Assuming field 'transport' is not equal to RTSP_TRANSPORT_RDT→
    13←Taking false branch→
2257  |         ret = ff_rdt_parse_packet(rtsp_st->transport_priv, pkt, &rt->recvbuf, len);
2258  |     } else if (rt->transport13.1Field 'transport' is not equal to RTSP_TRANSPORT_RTP == RTSP_TRANSPORT_RTP) {
2259  |         ret = ff_rtp_parse_packet(rtsp_st->transport_priv, pkt, &rt->recvbuf, len);
2260  |  if (rtsp_st->feedback) {
2261  |             AVIOContext *pb = NULL;
2262  |  if (rt->lower_transport == RTSP_LOWER_TRANSPORT_CUSTOM)
2263  |                 pb = s->pb;
2264  |             ff_rtp_send_rtcp_feedback(rtsp_st->transport_priv, rtsp_st->rtp_handle, pb);
2265  |         }
2266  |  if (ret < 0) {
2267  |  /* Either bad packet, or a RTCP packet. Check if the
2268  |  * first_rtcp_ntp_time field was initialized. */
2269  |             RTPDemuxContext *rtpctx = rtsp_st->transport_priv;
2270  |  if (rtpctx->first_rtcp_ntp_time != AV_NOPTS_VALUE) {
2271  |  /* first_rtcp_ntp_time has been initialized for this stream,
2272  |  * copy the same value to all other uninitialized streams,
2273  |  * in order to map their timestamp origin to the same ntp time
2274  |  * as this one. */
2275  |  int i;
2276  |                 AVStream *st = NULL;
2277  |  if (rtsp_st->stream_index >= 0)
2278  |                     st = s->streams[rtsp_st->stream_index];
2279  |  for (i = 0; i < rt->nb_rtsp_streams; i++) {
2280  |                     RTPDemuxContext *rtpctx2 = rt->rtsp_streams[i]->transport_priv;
2281  |                     AVStream *st2 = NULL;
2282  |  if (rt->rtsp_streams[i]->stream_index >= 0)
2283  |                         st2 = s->streams[rt->rtsp_streams[i]->stream_index];
2284  |  if (rtpctx2 && st && st2 &&
2285  |                         rtpctx2->first_rtcp_ntp_time == AV_NOPTS_VALUE) {
2286  |                         rtpctx2->first_rtcp_ntp_time = rtpctx->first_rtcp_ntp_time;
2287  |                         rtpctx2->rtcp_ts_offset = av_rescale_q(
2288  |                             rtpctx->rtcp_ts_offset, st->time_base,
2289  |                             st2->time_base);
2290  |                     }
2291  |                 }
2292  |  // Make real NTP start time available in AVFormatContext
2293  |  if (s->start_time_realtime == AV_NOPTS_VALUE) {
2294  |                     s->start_time_realtime = av_rescale (rtpctx->first_rtcp_ntp_time - (NTP_OFFSET << 32), 1000000, 1LL << 32);
2295  |  if (rtpctx->st) {
2296  |                         s->start_time_realtime -=
2297  |                             av_rescale_q (rtpctx->rtcp_ts_offset, rtpctx->st->time_base, AV_TIME_BASE_Q);
2298  |                     }
2299  |                 }
2300  |             }
2301  |  if (ret == -RTCP_BYE) {
2302  |                 rt->nb_byes++;
2303  |  
2304  |                 av_log(s, AV_LOG_DEBUG, "Received BYE for stream %d (%d/%d)\n",
2305  |                        rtsp_st->stream_index, rt->nb_byes, rt->nb_rtsp_streams);
2306  |  
2307  |  if (rt->nb_byes == rt->nb_rtsp_streams)
2308  |  return AVERROR_EOF;
2309  |             }
2310  |         }
2311  |     } else if (CONFIG_RTPDEC && rt->ts) {
    14←Assuming field 'ts' is non-null→
    15←Taking true branch→
2312  |  ret = avpriv_mpegts_parse_packet(rt->ts, pkt, rt->recvbuf, len);
2313  |  if (ret >= 0) {
    16←Assuming 'ret' is < 0→
    17←Taking false branch→
2314  |  if (ret < len) {
2315  |                 rt->recvbuf_len = len;
2316  |                 rt->recvbuf_pos = ret;
2317  |                 rt->cur_transport_priv = rt->ts;
2318  |  return 1;
2319  |             } else {
2320  |                 ret = 0;
2321  |             }
2322  |         }
2323  |     } else {
2324  |  return AVERROR_INVALIDDATA;
2325  |     }
2326  | end:
2327  |  if (ret17.1'ret' is < 0 < 0)
    18←Taking true branch→
2328  |  goto redo;
    19←Control jumps to line 2214→
2329  |  if (ret == 1)
2330  |  /* more packets may follow, so we save the RTP context */
2331  |         rt->cur_transport_priv = rtsp_st->transport_priv;
2332  |  
2333  |  return ret;
2334  | }
2335  | #endif /* CONFIG_RTPDEC */
2336  |  
2337  | #if CONFIG_SDP_DEMUXER
2338  | static int sdp_probe(const AVProbeData *p1)
2339  | {
2340  |  const char *p = p1->buf, *p_end = p1->buf + p1->buf_size;
2341  |  
2342  |  /* we look for a line beginning "c=IN IP" */
2343  |  while (p < p_end && *p != '\0') {
2344  |  if (sizeof("c=IN IP") - 1 < p_end - p &&
2345  |             av_strstart(p, "c=IN IP", NULL))
2346  |  return AVPROBE_SCORE_EXTENSION;
2347  |  
2348  |  while (p < p_end - 1 && *p != '\n') p++;
2349  |  if (++p >= p_end)
2350  |  break;
2351  |  if (*p == '\r')
2352  |             p++;
2353  |     }
2354  |  return 0;
2355  | }
2356  |  
2357  | static void append_source_addrs(char *buf, int size, const char *name,
2358  |  int count, struct RTSPSource **addrs)