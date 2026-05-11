### Report Summary

File:| format/rtsp.c  
---|---  
Warning:| line 2315, column 13  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


2253  |  if (fdsnum != 2) {
2254  |                     av_log(s, AV_LOG_ERROR,
2255  |  "Number of fds %d not supported\n", fdsnum);
2256  |                     av_freep(&fds);
2257  |  return AVERROR_INVALIDDATA;
2258  |                 }
2259  |  for (fdsidx = 0; fdsidx < fdsnum; fdsidx++) {
2260  |                     p[rt->max_p].fd       = fds[fdsidx];
2261  |                     p[rt->max_p++].events = POLLIN;
2262  |                 }
2263  |                 av_freep(&fds);
2264  |             }
2265  |         }
2266  |     }
2267  |  
2268  |  for (;;) {
2269  |  if (ff_check_interrupt(&s->interrupt_callback))
2270  |  return AVERROR_EXIT;
2271  |  if (wait_end && wait_end - av_gettime_relative() < 0)
2272  |  return AVERROR(EAGAIN);
2273  |         n = poll(p, rt->max_p, POLLING_TIME);
2274  |  if (n > 0) {
2275  |  int j = rt->rtsp_hd ? 1 : 0;
2276  |  for (i = 0; i < rt->nb_rtsp_streams; i++) {
2277  |                 rtsp_st = rt->rtsp_streams[i];
2278  |  if (rtsp_st->rtp_handle) {
2279  |  if (p[j].revents & POLLIN || p[j+1].revents & POLLIN) {
2280  |                         ret = ffurl_read(rtsp_st->rtp_handle, buf, buf_size);
2281  |  if (ret > 0) {
2282  |                             *prtsp_st = rtsp_st;
2283  |  return ret;
2284  |                         }
2285  |                     }
2286  |                     j+=2;
2287  |                 }
2288  |             }
2289  | #if CONFIG_RTSP_DEMUXER
2290  |  if (rt->rtsp_hd && p[0].revents & POLLIN) {
2291  |  if ((ret = parse_rtsp_message(s)) < 0) {
2292  |  return ret;
2293  |                 }
2294  |             }
2295  | #endif
2296  |         } else if (n == 0 && rt->stimeout > 0 && --runs <= 0) {
2297  |  return AVERROR(ETIMEDOUT);
2298  |         } else if (n < 0 && errno != EINTR)
2299  |  return AVERROR(errno);
2300  |     }
2301  | }
2302  |  
2303  | static int pick_stream(AVFormatContext *s, RTSPStream **rtsp_st,
2304  |  const uint8_t *buf, int len)
2305  | {
2306  |  RTSPState *rt = s->priv_data;
2307  |  int i;
2308  |  if (len < 0)
    15←Assuming 'len' is >= 0→
    16←Taking false branch→
2309  |  return len;
2310  |  if (rt->nb_rtsp_streams16.1Field 'nb_rtsp_streams' is not equal to 1 == 1) {
2311  |         *rtsp_st = rt->rtsp_streams[0];
2312  |  return len;
2313  |     }
2314  |  if (len >= 8 && rt->transport17.1Field 'transport' is equal to RTSP_TRANSPORT_RTP == RTSP_TRANSPORT_RTP) {
    17←Assuming 'len' is >= 8→
2315  |  if (RTP_PT_IS_RTCP(rt->recvbuf[1])) {
    18←Taking true branch→
    19←buffer read by avio_read may be partially uninitialized
2316  |  int no_ssrc = 0;
2317  |  for (i = 0; i < rt->nb_rtsp_streams; i++) {
2318  |                 RTPDemuxContext *rtpctx = rt->rtsp_streams[i]->transport_priv;
2319  |  if (!rtpctx)
2320  |  continue;
2321  |  if (rtpctx->ssrc == AV_RB32(&buf[4])) {
2322  |                     *rtsp_st = rt->rtsp_streams[i];
2323  |  return len;
2324  |                 }
2325  |  if (!rtpctx->ssrc)
2326  |                     no_ssrc = 1;
2327  |             }
2328  |  if (no_ssrc) {
2329  |                 av_log(s, AV_LOG_WARNING,
2330  |  "Unable to pick stream for packet - SSRC not known for "
2331  |  "all streams\n");
2332  |  return AVERROR(EAGAIN);
2333  |             }
2334  |         } else {
2335  |  for (i = 0; i < rt->nb_rtsp_streams; i++) {
2336  |  if ((buf[1] & 0x7f) == rt->rtsp_streams[i]->sdp_payload_type) {
2337  |                     *rtsp_st = rt->rtsp_streams[i];
2338  |  return len;
2339  |                 }
2340  |             }
2341  |         }
2342  |     }
2343  |     av_log(s, AV_LOG_WARNING, "Unable to pick stream for packet\n");
2344  |  return AVERROR(EAGAIN);
2345  | }
2346  |  
2347  | static int read_packet(AVFormatContext *s,
2348  |                        RTSPStream **rtsp_st, RTSPStream *first_queue_st,
2349  |                        int64_t wait_end)
2350  | {
2351  |  RTSPState *rt = s->priv_data;
2352  |  int len;
2353  |  
2354  |  switch(rt->lower_transport) {
    13←Control jumps to 'case RTSP_LOWER_TRANSPORT_CUSTOM:'  at line 2367→
2355  |  default:
2356  | #if CONFIG_RTSP_DEMUXER
2357  |  case RTSP_LOWER_TRANSPORT_TCP:
2358  |         len = ff_rtsp_tcp_read_packet(s, rtsp_st, rt->recvbuf, RECVBUF_SIZE);
2359  |  break;
2360  | #endif
2361  |  case RTSP_LOWER_TRANSPORT_UDP:
2362  |  case RTSP_LOWER_TRANSPORT_UDP_MULTICAST:
2363  |         len = udp_read_packet(s, rtsp_st, rt->recvbuf, RECVBUF_SIZE, wait_end);
2364  |  if (len > 0 && (*rtsp_st)->transport_priv && rt->transport == RTSP_TRANSPORT_RTP)
2365  |             ff_rtp_check_and_send_back_rr((*rtsp_st)->transport_priv, (*rtsp_st)->rtp_handle, NULL, len);
2366  |  break;
2367  |  case RTSP_LOWER_TRANSPORT_CUSTOM:
2368  |  if (first_queue_st13.1'first_queue_st' is null && rt->transport == RTSP_TRANSPORT_RTP &&
2369  |             wait_end && wait_end < av_gettime_relative())
2370  |             len = AVERROR(EAGAIN);
2371  |  else
2372  |  len = avio_read_partial(s->pb, rt->recvbuf, RECVBUF_SIZE);
2373  |  len = pick_stream(s, rtsp_st, rt->recvbuf, len);
    14←Calling 'pick_stream'→
2374  |  if (len > 0 && (*rtsp_st)->transport_priv && rt->transport == RTSP_TRANSPORT_RTP)
2375  |             ff_rtp_check_and_send_back_rr((*rtsp_st)->transport_priv, NULL, s->pb, len);
2376  |  break;
2377  |     }
2378  |  
2379  |  if (len == 0)
2380  |  return AVERROR_EOF;
2381  |  
2382  |  return len;
2383  | }
2384  |  
2385  | int ff_rtsp_fetch_packet(AVFormatContext *s, AVPacket *pkt)
2386  | {
2387  |  RTSPState *rt = s->priv_data;
2388  |  int ret, len;
2389  |     RTSPStream *rtsp_st, *first_queue_st = NULL;
2390  |     int64_t wait_end = 0;
2391  |  
2392  |  if (rt->nb_byes == rt->nb_rtsp_streams)
    1Assuming field 'nb_byes' is not equal to field 'nb_rtsp_streams'→
    2←Taking false branch→
2393  |  return AVERROR_EOF;
2394  |  
2395  |  /* get next frames from the same RTP packet */
2396  |  if (rt->cur_transport_priv) {
    3←Assuming field 'cur_transport_priv' is null→
    4←Taking false branch→
2397  |  if (rt->transport == RTSP_TRANSPORT_RDT) {
2398  |             ret = ff_rdt_parse_packet(rt->cur_transport_priv, pkt, NULL, 0);
2399  |         } else if (rt->transport == RTSP_TRANSPORT_RTP) {
2400  |             ret = ff_rtp_parse_packet(rt->cur_transport_priv, pkt, NULL, 0);
2401  |         } else if (CONFIG_RTPDEC && rt->ts) {
2402  |             ret = avpriv_mpegts_parse_packet(rt->ts, pkt, rt->recvbuf + rt->recvbuf_pos, rt->recvbuf_len - rt->recvbuf_pos);
2403  |  if (ret >= 0) {
2404  |                 rt->recvbuf_pos += ret;
2405  |                 ret = rt->recvbuf_pos < rt->recvbuf_len;
2406  |             }
2407  |         } else
2408  |             ret = -1;
2409  |  if (ret == 0) {
2410  |             rt->cur_transport_priv = NULL;
2411  |  return 0;
2412  |         } else if (ret == 1) {
2413  |  return 0;
2414  |         } else
2415  |             rt->cur_transport_priv = NULL;
2416  |     }
2417  |  
2418  | redo:
2419  |  if (rt->transport == RTSP_TRANSPORT_RTP) {
    5←Assuming field 'transport' is equal to RTSP_TRANSPORT_RTP→
    6←Taking true branch→
2420  |  int i;
2421  |         int64_t first_queue_time = 0;
2422  |  for (i = 0; i < rt->nb_rtsp_streams; i++) {
    7←Assuming 'i' is >= field 'nb_rtsp_streams'→
    8←Loop condition is false. Execution continues on line 2434→
2423  |             RTPDemuxContext *rtpctx = rt->rtsp_streams[i]->transport_priv;
2424  |             int64_t queue_time;
2425  |  if (!rtpctx)
2426  |  continue;
2427  |             queue_time = ff_rtp_queued_packet_time(rtpctx);
2428  |  if (queue_time && (queue_time - first_queue_time < 0 ||
2429  |                                !first_queue_time)) {
2430  |                 first_queue_time = queue_time;
2431  |                 first_queue_st   = rt->rtsp_streams[i];
2432  |             }
2433  |         }
2434  |  if (first_queue_time8.1'first_queue_time' is 0) {
    9←Taking false branch→
2435  |             wait_end = first_queue_time + s->max_delay;
2436  |         } else {
2437  |  wait_end = 0;
2438  |  first_queue_st = NULL;
2439  |         }
2440  |     }
2441  |  
2442  |  /* read next RTP packet */
2443  |  if (!rt->recvbuf) {
    10←Assuming field 'recvbuf' is non-null→
    11←Taking false branch→
2444  |         rt->recvbuf = av_malloc(RECVBUF_SIZE);
2445  |  if (!rt->recvbuf)
2446  |  return AVERROR(ENOMEM);
2447  |     }
2448  |  
2449  |  len = read_packet(s, &rtsp_st, first_queue_st, wait_end);
    12←Calling 'read_packet'→
2450  |  if (len == AVERROR(EAGAIN) && first_queue_st &&
2451  |         rt->transport == RTSP_TRANSPORT_RTP) {
2452  |         av_log(s, AV_LOG_WARNING,
2453  |  "max delay reached. need to consume packet\n");
2454  |         rtsp_st = first_queue_st;
2455  |         ret = ff_rtp_parse_packet(rtsp_st->transport_priv, pkt, NULL, 0);
2456  |  goto end;
2457  |     }
2458  |  if (len < 0)
2459  |  return len;
2460  |  
2461  |  if (rt->transport == RTSP_TRANSPORT_RDT) {
2462  |         ret = ff_rdt_parse_packet(rtsp_st->transport_priv, pkt, &rt->recvbuf, len);
2463  |     } else if (rt->transport == RTSP_TRANSPORT_RTP) {
2464  |         ret = ff_rtp_parse_packet(rtsp_st->transport_priv, pkt, &rt->recvbuf, len);
2465  |  if (rtsp_st->feedback) {
2466  |             AVIOContext *pb = NULL;
2467  |  if (rt->lower_transport == RTSP_LOWER_TRANSPORT_CUSTOM)
2468  |                 pb = s->pb;
2469  |             ff_rtp_send_rtcp_feedback(rtsp_st->transport_priv, rtsp_st->rtp_handle, pb);
2470  |         }
2471  |  if (ret < 0) {
2472  |  /* Either bad packet, or a RTCP packet. Check if the
2473  |  * first_rtcp_ntp_time field was initialized. */
2474  |             RTPDemuxContext *rtpctx = rtsp_st->transport_priv;
2475  |  if (rtpctx->first_rtcp_ntp_time != AV_NOPTS_VALUE) {
2476  |  /* first_rtcp_ntp_time has been initialized for this stream,
2477  |  * copy the same value to all other uninitialized streams,
2478  |  * in order to map their timestamp origin to the same ntp time
2479  |  * as this one. */