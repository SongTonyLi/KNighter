### Report Summary

File:| format/rtsp.c  
---|---  
Warning:| line 2597, column 27  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


84    |  { "pkt_size",           "Underlying protocol send packet size",                          OFFSET(pkt_size),              AV_OPT_TYPE_INT, { .i64 = 1472 }, -1, INT_MAX, ENC } \
85    |  
86    |  
87    | const AVOption ff_rtsp_options[] = {
88    |     { "initial_pause",  "do not start playing the stream immediately", OFFSET(initial_pause), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DEC },
89    |  FF_RTP_FLAG_OPTS(RTSPState, rtp_muxer_flags),
90    |     { "rtsp_transport", "set RTSP transport protocols", OFFSET(lower_transport_mask), AV_OPT_TYPE_FLAGS, {.i64 = 0}, INT_MIN, INT_MAX, DEC|ENC, .unit = "rtsp_transport" }, \
91    |     { "udp", "UDP", 0, AV_OPT_TYPE_CONST, {.i64 = 1 << RTSP_LOWER_TRANSPORT_UDP}, 0, 0, DEC|ENC, .unit = "rtsp_transport" }, \
92    |     { "tcp", "TCP", 0, AV_OPT_TYPE_CONST, {.i64 = 1 << RTSP_LOWER_TRANSPORT_TCP}, 0, 0, DEC|ENC, .unit = "rtsp_transport" }, \
93    |     { "udp_multicast", "UDP multicast", 0, AV_OPT_TYPE_CONST, {.i64 = 1 << RTSP_LOWER_TRANSPORT_UDP_MULTICAST}, 0, 0, DEC, .unit = "rtsp_transport" },
94    |     { "http", "HTTP tunneling", 0, AV_OPT_TYPE_CONST, {.i64 = (1 << RTSP_LOWER_TRANSPORT_HTTP)}, 0, 0, DEC, .unit = "rtsp_transport" },
95    |     { "https", "HTTPS tunneling", 0, AV_OPT_TYPE_CONST, {.i64 = (1 << RTSP_LOWER_TRANSPORT_HTTPS )}, 0, 0, DEC, .unit = "rtsp_transport" },
96    |  RTSP_FLAG_OPTS("rtsp_flags", "set RTSP flags"),
97    |     { "listen", "wait for incoming connections", 0, AV_OPT_TYPE_CONST, {.i64 = RTSP_FLAG_LISTEN}, 0, 0, DEC, .unit = "rtsp_flags" },
98    |     { "prefer_tcp", "try RTP via TCP first, if available", 0, AV_OPT_TYPE_CONST, {.i64 = RTSP_FLAG_PREFER_TCP}, 0, 0, DEC|ENC, .unit = "rtsp_flags" },
99    |     { "satip_raw", "export raw MPEG-TS stream instead of demuxing", 0, AV_OPT_TYPE_CONST, {.i64 = RTSP_FLAG_SATIP_RAW}, 0, 0, DEC, .unit = "rtsp_flags" },
100   |  RTSP_MEDIATYPE_OPTS("allowed_media_types", "set media types to accept from the server"),
101   |     { "min_port", "set minimum local UDP port", OFFSET(rtp_port_min), AV_OPT_TYPE_INT, {.i64 = RTSP_RTP_PORT_MIN}, 0, 65535, DEC|ENC },
102   |     { "max_port", "set maximum local UDP port", OFFSET(rtp_port_max), AV_OPT_TYPE_INT, {.i64 = RTSP_RTP_PORT_MAX}, 0, 65535, DEC|ENC },
103   |     { "listen_timeout", "set maximum timeout (in seconds) to wait for incoming connections (-1 is infinite, imply flag listen)", OFFSET(initial_timeout), AV_OPT_TYPE_INT, {.i64 = -1}, INT_MIN, INT_MAX, DEC },
104   |     { "timeout", "set timeout (in microseconds) of socket I/O operations", OFFSET(stimeout), AV_OPT_TYPE_INT64, {.i64 = 0}, INT_MIN, INT64_MAX, DEC },
105   |  COMMON_OPTS(),
106   |     { "user_agent", "override User-Agent header", OFFSET(user_agent), AV_OPT_TYPE_STRING, {.str = LIBAVFORMAT_IDENT}, 0, 0, DEC },
107   |  
108   |  // TLS options
109   |  FF_TLS_CLIENT_OPTIONS(RTSPState, tls_opts),
110   |     { NULL },
111   | };
112   |  
113   | static const AVOption sdp_options[] = {
114   |  RTSP_FLAG_OPTS("sdp_flags", "SDP flags"),
115   |     { "custom_io", "use custom I/O", 0, AV_OPT_TYPE_CONST, {.i64 = RTSP_FLAG_CUSTOM_IO}, 0, 0, DEC, .unit = "rtsp_flags" },
116   |     { "rtcp_to_source", "send RTCP packets to the source address of received packets", 0, AV_OPT_TYPE_CONST, {.i64 = RTSP_FLAG_RTCP_TO_SOURCE}, 0, 0, DEC, .unit = "rtsp_flags" },
117   |     { "listen_timeout", "set maximum timeout (in seconds) to wait for incoming connections", OFFSET(stimeout), AV_OPT_TYPE_DURATION, {.i64 = READ_PACKET_TIMEOUT_S*1000000}, INT_MIN, INT64_MAX, DEC },
118   |     { "localaddr",          "local address",                                                 OFFSET(localaddr),AV_OPT_TYPE_STRING,   {.str = NULL}, 0, 0, DEC }, \
119   |  RTSP_MEDIATYPE_OPTS("allowed_media_types", "set media types to accept from the server"),
120   |  COMMON_OPTS(),
121   |     { NULL },
122   | };
123   |  
124   | static const AVOption rtp_options[] = {
125   |  RTSP_FLAG_OPTS("rtp_flags", "set RTP flags"),
126   |     { "listen_timeout", "set maximum timeout (in seconds) to wait for incoming connections", OFFSET(stimeout), AV_OPT_TYPE_DURATION, {.i64 = READ_PACKET_TIMEOUT_S*1000000}, INT_MIN, INT64_MAX, DEC },
127   |     { "localaddr",          "local address",                                                 OFFSET(localaddr),AV_OPT_TYPE_STRING,   {.str = NULL}, 0, 0, DEC }, \
128   |  RTSP_MEDIATYPE_OPTS("allowed_media_types", "set media types to accept from the server"),
129   |  COMMON_OPTS(),
130   |     { NULL },
131   | };
132   |  
133   |  
134   | static AVDictionary *map_to_opts(RTSPState *rt)
135   | {
136   |     AVDictionary *opts = NULL;
137   |  
138   |     av_dict_set_int(&opts, "buffer_size", rt->buffer_size, 0);
139   |     av_dict_set_int(&opts, "pkt_size",    rt->pkt_size,    0);
140   |  if (rt->localaddr && rt->localaddr[0])
141   |         av_dict_set(&opts, "localaddr", rt->localaddr, 0);
142   |  
143   |  return opts;
144   | }
145   |  
146   | #define ERR_RET(c)      \
147   |  do {                \
148   |  int ret = c;    \
149   |  if (ret < 0)    \
150   |  return ret; \
151   |  } while (0)
152   |  
153   | /**
154   |  * Add the TLS options of the given RTSPState to the dict
155   |  */
156   | static int copy_tls_opts_dict(RTSPState *rt, AVDictionary **dict)
157   | {
158   |     ERR_RET(av_dict_set_int(dict, "tls_verify", rt->tls_opts.verify, 0));
159   |     ERR_RET(av_dict_set(dict, "ca_file", rt->tls_opts.ca_file, 0));
160   |     ERR_RET(av_dict_set(dict, "cert_file", rt->tls_opts.cert_file, 0));
161   |     ERR_RET(av_dict_set(dict, "key_file", rt->tls_opts.key_file, 0));
162   |     ERR_RET(av_dict_set(dict, "verifyhost", rt->tls_opts.host, 0));
163   |  
164   |  return 0;
165   | }
166   |  
167   | #undef ERR_RET
168   |  
169   | static void get_word_until_chars(char *buf, int buf_size,
170   |  const char *sep, const char **pp)
171   | {
172   |  const char *p;
173   |  char *q;
2523  |  return 1;
2524  |             } else {
2525  |                 ret = 0;
2526  |             }
2527  |         }
2528  |     } else {
2529  |  return AVERROR_INVALIDDATA;
2530  |     }
2531  | end:
2532  |  if (ret < 0)
2533  |  goto redo;
2534  |  if (ret == 1)
2535  |  /* more packets may follow, so we save the RTP context */
2536  |         rt->cur_transport_priv = rtsp_st->transport_priv;
2537  |  
2538  |  return ret;
2539  | }
2540  | #endif /* CONFIG_RTPDEC */
2541  |  
2542  | #if CONFIG_SDP_DEMUXER
2543  | static int sdp_probe(const AVProbeData *p1)
2544  | {
2545  |  const char *p = p1->buf, *p_end = p1->buf + p1->buf_size;
2546  |  
2547  |  /* we look for a line beginning "c=IN IP" */
2548  |  while (p < p_end && *p != '\0') {
2549  |  if (sizeof("c=IN IP") - 1 < p_end - p &&
2550  |             av_strstart(p, "c=IN IP", NULL))
2551  |  return AVPROBE_SCORE_EXTENSION;
2552  |  
2553  |  while (p < p_end - 1 && *p != '\n') p++;
2554  |  if (++p >= p_end)
2555  |  break;
2556  |  if (*p == '\r')
2557  |             p++;
2558  |     }
2559  |  return 0;
2560  | }
2561  |  
2562  | static void append_source_addrs(char *buf, int size, const char *name,
2563  |  int count, struct RTSPSource **addrs)
2564  | {
2565  |  int i;
2566  |  if (!count)
2567  |  return;
2568  |     av_strlcatf(buf, size, "&%s=%s", name, addrs[0]->addr);
2569  |  for (i = 1; i < count; i++)
2570  |         av_strlcatf(buf, size, ",%s", addrs[i]->addr);
2571  | }
2572  |  
2573  | static int sdp_read_header(AVFormatContext *s)
2574  | {
2575  |  RTSPState *rt = s->priv_data;
2576  |     RTSPStream *rtsp_st;
2577  |  int i, err;
2578  |  char url[MAX_URL_SIZE];
2579  |     AVBPrint bp;
2580  |  
2581  |  if (!ff_network_init())
    28←Assuming the condition is false→
    29←Taking false branch→
2582  |  return AVERROR(EIO);
2583  |  
2584  |  if (s->max_delay < 0) /* Not set by the caller */
    30←Assuming field 'max_delay' is >= 0→
    31←Taking false branch→
2585  |         s->max_delay = DEFAULT_REORDERING_DELAY;
2586  |  if (rt->rtsp_flags & RTSP_FLAG_CUSTOM_IO)
    32←Assuming the condition is false→
    33←Taking false branch→
2587  |         rt->lower_transport = RTSP_LOWER_TRANSPORT_CUSTOM;
2588  |  
2589  |  /* read the whole sdp file */
2590  |  av_bprint_init(&bp, 0, AV_BPRINT_SIZE_UNLIMITED);
2591  |     err = avio_read_to_bprint(s->pb, &bp, INT_MAX);
2592  |  if (err < 0 ) {
    34←Assuming 'err' is >= 0→
    35←Taking false branch→
2593  |         ff_network_close();
2594  |         av_bprint_finalize(&bp, NULL);
2595  |  return err;
2596  |     }
2597  |  err = ff_sdp_parse(s, bp.str);
    36←buffer read by avio_read may be partially uninitialized
2598  |     av_bprint_finalize(&bp, NULL);
2599  |  if (err) goto fail;
2600  |  
2601  |  /* open each RTP stream */
2602  |  for (i = 0; i < rt->nb_rtsp_streams; i++) {
2603  |  char namebuf[50];
2604  |         rtsp_st = rt->rtsp_streams[i];
2605  |  
2606  |  if (!(rt->rtsp_flags & RTSP_FLAG_CUSTOM_IO)) {
2607  |             AVDictionary *opts = map_to_opts(rt);
2608  |  char buf[MAX_URL_SIZE];
2609  |  const char *p;
2610  |  
2611  |             err = getnameinfo((struct sockaddr*) &rtsp_st->sdp_ip,
2612  |  sizeof(rtsp_st->sdp_ip),
2613  |                               namebuf, sizeof(namebuf), NULL, 0, NI_NUMERICHOST);
2614  |  if (err) {
2615  |                 av_log(s, AV_LOG_ERROR, "getnameinfo: %s\n", gai_strerror(err));
2616  |                 err = AVERROR(EIO);
2617  |                 av_dict_free(&opts);
2618  |  goto fail;
2619  |             }
2620  |             ff_url_join(url, sizeof(url), "rtp", NULL,
2621  |                         namebuf, rtsp_st->sdp_port,
2622  |  "?localrtpport=%d&ttl=%d&connect=%d&write_to_source=%d",
2623  |                         rtsp_st->sdp_port, rtsp_st->sdp_ttl,
2624  |                         rt->rtsp_flags & RTSP_FLAG_FILTER_SRC ? 1 : 0,
2625  |                         rt->rtsp_flags & RTSP_FLAG_RTCP_TO_SOURCE ? 1 : 0);
2626  |  
2627  |             p = strchr(s->url, '?');
2642  |  
2643  |  if (err < 0) {
2644  |                 err = AVERROR_INVALIDDATA;
2645  |  goto fail;
2646  |             }
2647  |         }
2648  |  if ((err = ff_rtsp_open_transport_ctx(s, rtsp_st)))
2649  |  goto fail;
2650  |     }
2651  |  return 0;
2652  | fail:
2653  |     ff_rtsp_close_streams(s);
2654  |     ff_network_close();
2655  |  return err;
2656  | }
2657  |  
2658  | static int sdp_read_close(AVFormatContext *s)
2659  | {
2660  |     ff_rtsp_close_streams(s);
2661  |     ff_network_close();
2662  |  return 0;
2663  | }
2664  |  
2665  | static const AVClass sdp_demuxer_class = {
2666  |     .class_name     = "SDP demuxer",
2667  |     .item_name      = av_default_item_name,
2668  |     .option         = sdp_options,
2669  |     .version        = LIBAVUTIL_VERSION_INT,
2670  | };
2671  |  
2672  | const FFInputFormat ff_sdp_demuxer = {
2673  |     .p.name         = "sdp",
2674  |     .p.long_name    = NULL_IF_CONFIG_SMALL("SDP"),
2675  |     .p.priv_class   = &sdp_demuxer_class,
2676  |     .priv_data_size = sizeof(RTSPState),
2677  |     .read_probe     = sdp_probe,
2678  |     .read_header    = sdp_read_header,
2679  |     .read_packet    = ff_rtsp_fetch_packet,
2680  |     .read_close     = sdp_read_close,
2681  | };
2682  | #endif /* CONFIG_SDP_DEMUXER */
2683  |  
2684  | #if CONFIG_RTP_DEMUXER
2685  | static int rtp_probe(const AVProbeData *p)
2686  | {
2687  |  if (av_strstart(p->filename, "rtp:", NULL))
2688  |  return AVPROBE_SCORE_MAX;
2689  |  return 0;
2690  | }
2691  |  
2692  | static int rtp_read_header(AVFormatContext *s)
2693  | {
2694  |  uint8_t recvbuf[RTP_MAX_PACKET_LENGTH];
2695  |  char host[500], filters_buf[1000];
2696  |  int ret, port;
2697  |     URLContext* in = NULL;
2698  |  int payload_type;
2699  |     AVCodecParameters *par = NULL;
2700  |  struct sockaddr_storage addr;
2701  |     FFIOContext pb;
2702  |     socklen_t addrlen = sizeof(addr);
2703  |     RTSPState *rt = s->priv_data;
2704  |  const char *p;
2705  |     AVBPrint sdp;
2706  |     AVDictionary *opts = NULL;
2707  |  
2708  |  if (!ff_network_init())
    1Assuming the condition is false→
    2←Taking false branch→
2709  |  return AVERROR(EIO);
2710  |  
2711  |  opts = map_to_opts(rt);
2712  |     ret = ffurl_open_whitelist(&in, s->url, AVIO_FLAG_READ,
2713  |                      &s->interrupt_callback, &opts, s->protocol_whitelist, s->protocol_blacklist, NULL);
2714  |     av_dict_free(&opts);
2715  |  if (ret)
    3←Assuming 'ret' is 0→
    4←Taking false branch→
2716  |  goto fail;
2717  |  
2718  |  while (1) {
    5←Loop condition is true.  Entering loop body→
2719  |  ret = ffurl_read(in, recvbuf, sizeof(recvbuf));
2720  |  if (ret == AVERROR(EAGAIN))
    6←Assuming the condition is false→
    7←Taking false branch→
2721  |  continue;
2722  |  if (ret < 0)
    8←Assuming 'ret' is >= 0→
    9←Taking false branch→
2723  |  goto fail;
2724  |  if (ret < 12) {
    10←Assuming 'ret' is >= 12→
    11←Taking false branch→
2725  |             av_log(s, AV_LOG_WARNING, "Received too short packet\n");
2726  |  continue;
2727  |         }
2728  |  
2729  |  if ((recvbuf[0] & 0xc0) != 0x80) {
    12←Assuming the condition is false→
2730  |             av_log(s, AV_LOG_WARNING, "Unsupported RTP version packet "
2731  |  "received\n");
2732  |  continue;
2733  |         }
2734  |  
2735  |  if (RTP_PT_IS_RTCP(recvbuf[1]))
    13←Taking false branch→
    14←Assuming the condition is false→
2736  |  continue;
2737  |  
2738  |  payload_type = recvbuf[1] & 0x7f;
2739  |  break;
2740  |     }
2741  |  getsockname(ffurl_get_file_handle(in), (struct sockaddr*) &addr, &addrlen);
2742  |     ffurl_closep(&in);
2743  |  
2744  |     par = avcodec_parameters_alloc();
2745  |  if (!par) {
    15←Assuming 'par' is non-null→
    16←Taking false branch→
2746  |         ret = AVERROR(ENOMEM);
2747  |  goto fail;
2748  |     }
2749  |  
2750  |  if (ff_rtp_get_codec_info(par, payload_type)) {
    17←Assuming the condition is false→
    18←Taking false branch→
2751  |         av_log(s, AV_LOG_ERROR, "Unable to receive RTP payload type %d "
2752  |  "without an SDP file describing it\n",
2753  |                                  payload_type);
2754  |         ret = AVERROR_INVALIDDATA;
2755  |  goto fail;
2756  |     }
2757  |  if (par->codec_type != AVMEDIA_TYPE_DATA) {
    19←Assuming field 'codec_type' is equal to AVMEDIA_TYPE_DATA→
    20←Taking false branch→
2758  |         av_log(s, AV_LOG_WARNING, "Guessing on RTP content - if not received "
2759  |  "properly you need an SDP file "
2760  |  "describing it\n");
2761  |     }
2762  |  
2763  |  av_url_split(NULL, 0, NULL, 0, host, sizeof(host), &port,
2764  |  NULL, 0, s->url);
2765  |  
2766  |     av_bprint_init(&sdp, 0, AV_BPRINT_SIZE_UNLIMITED);
2767  |  av_bprintf(&sdp, "v=0\r\nc=IN IP%d %s\r\n",
2768  |  addr.ss_family == AF_INET ? 4 : 6, host);
    21←Assuming field 'ss_family' is not equal to AF_INET→
    22←'?' condition is false→
2769  |  
2770  |  p = strchr(s->url, '?');
2771  |  if (p) {
    23←Assuming 'p' is null→
    24←Taking false branch→
2772  |  static const char filters[][2][8] = { { "sources", "incl" },
2773  |                                               { "block",   "excl" } };
2774  |  int i;
2775  |  char *q;
2776  |  for (i = 0; i < FF_ARRAY_ELEMS(filters); i++) {
2777  |  if (av_find_info_tag(filters_buf, sizeof(filters_buf), filters[i][0], p)) {
2778  |                 q = filters_buf;
2779  |  while ((q = strchr(q, ',')) != NULL)
2780  |                     *q = ' ';
2781  |                 av_bprintf(&sdp, "a=source-filter:%s IN IP%d %s %s\r\n",
2782  |                            filters[i][1],
2783  |                            addr.ss_family == AF_INET ? 4 : 6, host,
2784  |                            filters_buf);
2785  |             }
2786  |         }
2787  |     }
2788  |  
2789  |  av_bprintf(&sdp, "m=%s %d RTP/AVP %d\r\n",
2790  |  par->codec_type24.1Field 'codec_type' is equal to AVMEDIA_TYPE_DATA == AVMEDIA_TYPE_DATA  ? "application" :
    25←'?' condition is true→
2791  |                par->codec_type == AVMEDIA_TYPE_VIDEO ? "video" : "audio",
2792  |  port, payload_type);
2793  |  av_log(s, AV_LOG_VERBOSE, "SDP:\n%s\n", sdp.str);
2794  |  if (!av_bprint_is_complete(&sdp))
    26←Taking false branch→
2795  |  goto fail_nobuf;
2796  |  avcodec_parameters_free(&par);
2797  |  
2798  |     ffio_init_read_context(&pb, sdp.str, sdp.len);
2799  |     s->pb = &pb.pub;
2800  |  
2801  |  /* if sdp_read_header() fails then following ff_network_close() cancels out */
2802  |  /* ff_network_init() at the start of this function. Otherwise it cancels out */
2803  |  /* ff_network_init() inside sdp_read_header() */
2804  |     ff_network_close();
2805  |  
2806  |     rt->media_type_mask = (1 << (AVMEDIA_TYPE_SUBTITLE+1)) - 1;
2807  |  
2808  |  ret = sdp_read_header(s);
    27←Calling 'sdp_read_header'→
2809  |     s->pb = NULL;
2810  |     av_bprint_finalize(&sdp, NULL);
2811  |  return ret;
2812  |  
2813  | fail_nobuf:
2814  |     ret = AVERROR(ENOMEM);
2815  |     av_log(s, AV_LOG_ERROR, "rtp_read_header(): not enough buffer space for sdp-headers\n");
2816  |     av_bprint_finalize(&sdp, NULL);
2817  | fail:
2818  |     avcodec_parameters_free(&par);
2819  |     ffurl_closep(&in);
2820  |     ff_network_close();
2821  |  return ret;
2822  | }
2823  |  
2824  | static const AVClass rtp_demuxer_class = {
2825  |     .class_name     = "RTP demuxer",
2826  |     .item_name      = av_default_item_name,
2827  |     .option         = rtp_options,
2828  |     .version        = LIBAVUTIL_VERSION_INT,
2829  | };
2830  |  
2831  | const FFInputFormat ff_rtp_demuxer = {
2832  |     .p.name         = "rtp",
2833  |     .p.long_name    = NULL_IF_CONFIG_SMALL("RTP input"),
2834  |     .p.flags        = AVFMT_NOFILE,
2835  |     .p.priv_class   = &rtp_demuxer_class,
2836  |     .priv_data_size = sizeof(RTSPState),
2837  |     .read_probe     = rtp_probe,
2838  |     .read_header    = rtp_read_header,