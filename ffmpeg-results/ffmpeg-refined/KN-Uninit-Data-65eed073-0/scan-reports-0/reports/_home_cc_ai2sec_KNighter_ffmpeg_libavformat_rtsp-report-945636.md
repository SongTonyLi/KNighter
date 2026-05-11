### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/rtsp.c  
---|---  
Warning:| line 722, column 13  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


72    |  { "subtitle", "Subtitle", 0, AV_OPT_TYPE_CONST, {.i64 = 1 << AVMEDIA_TYPE_SUBTITLE}, 0, 0, DEC, "allowed_media_types" }
73    |  
74    | #define COMMON_OPTS() \
75    |  { "reorder_queue_size", "set number of packets to buffer for handling of reordered packets", OFFSET(reordering_queue_size), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, DEC }, \
76    |  { "buffer_size",        "Underlying protocol send/receive buffer size",                  OFFSET(buffer_size),           AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, DEC|ENC }, \
77    |  { "pkt_size",           "Underlying protocol send packet size",                          OFFSET(pkt_size),              AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, ENC } \
78    |  
79    |  
80    | const AVOption ff_rtsp_options[] = {
81    |     { "initial_pause",  "do not start playing the stream immediately", OFFSET(initial_pause), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DEC },
82    |  FF_RTP_FLAG_OPTS(RTSPState, rtp_muxer_flags),
83    |     { "rtsp_transport", "set RTSP transport protocols", OFFSET(lower_transport_mask), AV_OPT_TYPE_FLAGS, {.i64 = 0}, INT_MIN, INT_MAX, DEC|ENC, "rtsp_transport" }, \
84    |     { "udp", "UDP", 0, AV_OPT_TYPE_CONST, {.i64 = 1 << RTSP_LOWER_TRANSPORT_UDP}, 0, 0, DEC|ENC, "rtsp_transport" }, \
85    |     { "tcp", "TCP", 0, AV_OPT_TYPE_CONST, {.i64 = 1 << RTSP_LOWER_TRANSPORT_TCP}, 0, 0, DEC|ENC, "rtsp_transport" }, \
86    |     { "udp_multicast", "UDP multicast", 0, AV_OPT_TYPE_CONST, {.i64 = 1 << RTSP_LOWER_TRANSPORT_UDP_MULTICAST}, 0, 0, DEC, "rtsp_transport" },
87    |     { "http", "HTTP tunneling", 0, AV_OPT_TYPE_CONST, {.i64 = (1 << RTSP_LOWER_TRANSPORT_HTTP)}, 0, 0, DEC, "rtsp_transport" },
88    |     { "https", "HTTPS tunneling", 0, AV_OPT_TYPE_CONST, {.i64 = (1 << RTSP_LOWER_TRANSPORT_HTTPS )}, 0, 0, DEC, "rtsp_transport" },
89    |  RTSP_FLAG_OPTS("rtsp_flags", "set RTSP flags"),
90    |     { "listen", "wait for incoming connections", 0, AV_OPT_TYPE_CONST, {.i64 = RTSP_FLAG_LISTEN}, 0, 0, DEC, "rtsp_flags" },
91    |     { "prefer_tcp", "try RTP via TCP first, if available", 0, AV_OPT_TYPE_CONST, {.i64 = RTSP_FLAG_PREFER_TCP}, 0, 0, DEC|ENC, "rtsp_flags" },
92    |     { "satip_raw", "export raw MPEG-TS stream instead of demuxing", 0, AV_OPT_TYPE_CONST, {.i64 = RTSP_FLAG_SATIP_RAW}, 0, 0, DEC, "rtsp_flags" },
93    |  RTSP_MEDIATYPE_OPTS("allowed_media_types", "set media types to accept from the server"),
94    |     { "min_port", "set minimum local UDP port", OFFSET(rtp_port_min), AV_OPT_TYPE_INT, {.i64 = RTSP_RTP_PORT_MIN}, 0, 65535, DEC|ENC },
95    |     { "max_port", "set maximum local UDP port", OFFSET(rtp_port_max), AV_OPT_TYPE_INT, {.i64 = RTSP_RTP_PORT_MAX}, 0, 65535, DEC|ENC },
96    |     { "listen_timeout", "set maximum timeout (in seconds) to wait for incoming connections (-1 is infinite, imply flag listen)", OFFSET(initial_timeout), AV_OPT_TYPE_INT, {.i64 = -1}, INT_MIN, INT_MAX, DEC },
97    |     { "timeout", "set timeout (in microseconds) of socket TCP I/O operations", OFFSET(stimeout), AV_OPT_TYPE_INT, {.i64 = 0}, INT_MIN, INT_MAX, DEC },
98    |  COMMON_OPTS(),
99    |     { "user_agent", "override User-Agent header", OFFSET(user_agent), AV_OPT_TYPE_STRING, {.str = LIBAVFORMAT_IDENT}, 0, 0, DEC },
100   |     { NULL },
101   | };
102   |  
103   | static const AVOption sdp_options[] = {
104   |  RTSP_FLAG_OPTS("sdp_flags", "SDP flags"),
105   |     { "custom_io", "use custom I/O", 0, AV_OPT_TYPE_CONST, {.i64 = RTSP_FLAG_CUSTOM_IO}, 0, 0, DEC, "rtsp_flags" },
106   |     { "rtcp_to_source", "send RTCP packets to the source address of received packets", 0, AV_OPT_TYPE_CONST, {.i64 = RTSP_FLAG_RTCP_TO_SOURCE}, 0, 0, DEC, "rtsp_flags" },
107   |     { "listen_timeout", "set maximum timeout (in seconds) to wait for incoming connections", OFFSET(initial_timeout), AV_OPT_TYPE_INT, {.i64 = READ_PACKET_TIMEOUT_S}, INT_MIN, INT_MAX, DEC },
108   |  RTSP_MEDIATYPE_OPTS("allowed_media_types", "set media types to accept from the server"),
109   |  COMMON_OPTS(),
110   |     { NULL },
111   | };
112   |  
113   | static const AVOption rtp_options[] = {
114   |  RTSP_FLAG_OPTS("rtp_flags", "set RTP flags"),
115   |     { "listen_timeout", "set maximum timeout (in seconds) to wait for incoming connections", OFFSET(initial_timeout), AV_OPT_TYPE_INT, {.i64 = READ_PACKET_TIMEOUT_S}, INT_MIN, INT_MAX, DEC },
116   |  RTSP_MEDIATYPE_OPTS("allowed_media_types", "set media types to accept from the server"),
117   |  COMMON_OPTS(),
118   |     { NULL },
119   | };
120   |  
121   |  
122   | static AVDictionary *map_to_opts(RTSPState *rt)
123   | {
124   |     AVDictionary *opts = NULL;
125   |  char buf[256];
126   |  
127   |     snprintf(buf, sizeof(buf), "%d", rt->buffer_size);
128   |     av_dict_set(&opts, "buffer_size", buf, 0);
129   |     snprintf(buf, sizeof(buf), "%d", rt->pkt_size);
130   |     av_dict_set(&opts, "pkt_size", buf, 0);
131   |  
132   |  return opts;
133   | }
134   |  
135   | static void get_word_until_chars(char *buf, int buf_size,
136   |  const char *sep, const char **pp)
137   | {
138   |  const char *p;
139   |  char *q;
140   |  
141   |     p = *pp;
142   |     p += strspn(p, SPACE_CHARS);
143   |     q = buf;
144   |  while (!strchr(sep, *p) && *p != '\0') {
145   |  if ((q - buf) < buf_size - 1)
146   |             *q++ = *p;
147   |         p++;
148   |     }
149   |  if (buf_size > 0)
150   |         *q = '\0';
151   |     *pp = p;
152   | }
153   |  
154   | static void get_word_sep(char *buf, int buf_size, const char *sep,
155   |  const char **pp)
156   | {
157   |  if (**pp == '/') (*pp)++;
158   |     get_word_until_chars(buf, buf_size, sep, pp);
159   | }
160   |  
161   | static void get_word(char *buf, int buf_size, const char **pp)
162   | {
658   |             get_word(buf1, sizeof(buf1), &p);
659   |  if (strcmp(buf1, "IN") != 0)
660   |  return;
661   |             get_word(buf1, sizeof(buf1), &p);
662   |  if (strcmp(buf1, "IP4") && strcmp(buf1, "IP6") && strcmp(buf1, "*"))
663   |  return;
664   |  // not checking that the destination address actually matches or is wildcard
665   |             get_word(buf1, sizeof(buf1), &p);
666   |  
667   |  while (*p != '\0') {
668   |                 rtsp_src = av_mallocz(sizeof(*rtsp_src));
669   |  if (!rtsp_src)
670   |  return;
671   |                 get_word(rtsp_src->addr, sizeof(rtsp_src->addr), &p);
672   |  if (exclude) {
673   |  if (s->nb_streams == 0) {
674   |  dynarray_add(&s1->default_exclude_source_addrs, &s1->nb_default_exclude_source_addrs, rtsp_src);
675   |                     } else {
676   |                         rtsp_st = rt->rtsp_streams[rt->nb_rtsp_streams - 1];
677   |  dynarray_add(&rtsp_st->exclude_source_addrs, &rtsp_st->nb_exclude_source_addrs, rtsp_src);
678   |                     }
679   |                 } else {
680   |  if (s->nb_streams == 0) {
681   |  dynarray_add(&s1->default_include_source_addrs, &s1->nb_default_include_source_addrs, rtsp_src);
682   |                     } else {
683   |                         rtsp_st = rt->rtsp_streams[rt->nb_rtsp_streams - 1];
684   |  dynarray_add(&rtsp_st->include_source_addrs, &rtsp_st->nb_include_source_addrs, rtsp_src);
685   |                     }
686   |                 }
687   |             }
688   |         } else {
689   |  if (rt->server_type == RTSP_SERVER_WMS)
690   |                 ff_wms_parse_sdp_a_line(s, p);
691   |  if (s->nb_streams > 0) {
692   |                 rtsp_st = rt->rtsp_streams[rt->nb_rtsp_streams - 1];
693   |  
694   |  if (rt->server_type == RTSP_SERVER_REAL)
695   |                     ff_real_parse_sdp_a_line(s, rtsp_st->stream_index, p);
696   |  
697   |  if (rtsp_st->dynamic_handler &&
698   |                     rtsp_st->dynamic_handler->parse_sdp_a_line)
699   |                     rtsp_st->dynamic_handler->parse_sdp_a_line(s,
700   |                         rtsp_st->stream_index,
701   |                         rtsp_st->dynamic_protocol_context, buf);
702   |             }
703   |         }
704   |  break;
705   |     }
706   | }
707   |  
708   | int ff_sdp_parse(AVFormatContext *s, const char *content)
709   | {
710   |  const char *p;
711   |  int letter, i;
712   |  char buf[SDP_MAX_SIZE], *q;
713   |     SDPParseState sdp_parse_state = { { 0 } }, *s1 = &sdp_parse_state;
714   |  
715   |     p = content;
716   |  for (;;) {
    39←Loop condition is true.  Entering loop body→
717   |  p += strspn(p, SPACE_CHARS);
718   |         letter = *p;
719   |  if (letter == '\0')
    40←Assuming the condition is false→
    41←Taking false branch→
720   |  break;
721   |  p++;
722   |  if (*p != '=')
    42←buffer read by avio_read may be partially uninitialized
723   |  goto next_line;
724   |         p++;
725   |  /* get the content */
726   |         q = buf;
727   |  while (*p != '\n' && *p != '\r' && *p != '\0') {
728   |  if ((q - buf) < sizeof(buf) - 1)
729   |                 *q++ = *p;
730   |             p++;
731   |         }
732   |         *q = '\0';
733   |         sdp_parse_line(s, s1, letter, buf);
734   |     next_line:
735   |  while (*p != '\n' && *p != '\0')
736   |             p++;
737   |  if (*p == '\n')
738   |             p++;
739   |     }
740   |  
741   |  for (i = 0; i < s1->nb_default_include_source_addrs; i++)
742   |         av_freep(&s1->default_include_source_addrs[i]);
743   |     av_freep(&s1->default_include_source_addrs);
744   |  for (i = 0; i < s1->nb_default_exclude_source_addrs; i++)
745   |         av_freep(&s1->default_exclude_source_addrs[i]);
746   |     av_freep(&s1->default_exclude_source_addrs);
747   |  
748   |  return 0;
749   | }
750   | #endif /* CONFIG_RTPDEC */
751   |  
752   | void ff_rtsp_undo_setup(AVFormatContext *s, int send_packets)
2318  |  return 1;
2319  |             } else {
2320  |                 ret = 0;
2321  |             }
2322  |         }
2323  |     } else {
2324  |  return AVERROR_INVALIDDATA;
2325  |     }
2326  | end:
2327  |  if (ret < 0)
2328  |  goto redo;
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
2359  | {
2360  |  int i;
2361  |  if (!count)
2362  |  return;
2363  |     av_strlcatf(buf, size, "&%s=%s", name, addrs[0]->addr);
2364  |  for (i = 1; i < count; i++)
2365  |         av_strlcatf(buf, size, ",%s", addrs[i]->addr);
2366  | }
2367  |  
2368  | static int sdp_read_header(AVFormatContext *s)
2369  | {
2370  |  RTSPState *rt = s->priv_data;
2371  |     RTSPStream *rtsp_st;
2372  |  int size, i, err;
2373  |  char *content;
2374  |  char url[MAX_URL_SIZE];
2375  |  
2376  |  if (!ff_network_init())
    28←Assuming the condition is false→
    29←Taking false branch→
2377  |  return AVERROR(EIO);
2378  |  
2379  |  if (s->max_delay < 0) /* Not set by the caller */
    30←Assuming field 'max_delay' is >= 0→
    31←Taking false branch→
2380  |         s->max_delay = DEFAULT_REORDERING_DELAY;
2381  |  if (rt->rtsp_flags & RTSP_FLAG_CUSTOM_IO)
    32←Assuming the condition is false→
    33←Taking false branch→
2382  |         rt->lower_transport = RTSP_LOWER_TRANSPORT_CUSTOM;
2383  |  
2384  |  /* read the whole sdp file */
2385  |  /* XXX: better loading */
2386  |  content = av_malloc(SDP_MAX_SIZE);
2387  |  if (!content) {
    34←Assuming 'content' is non-null→
    35←Taking false branch→
2388  |         ff_network_close();
2389  |  return AVERROR(ENOMEM);
2390  |     }
2391  |  size = avio_read(s->pb, content, SDP_MAX_SIZE - 1);
2392  |  if (size <= 0) {
    36←Assuming 'size' is > 0→
    37←Taking false branch→
2393  |         av_free(content);
2394  |         ff_network_close();
2395  |  return AVERROR_INVALIDDATA;
2396  |     }
2397  |  content[size] ='\0';
2398  |  
2399  |  err = ff_sdp_parse(s, content);
    38←Calling 'ff_sdp_parse'→
2400  |     av_freep(&content);
2401  |  if (err) goto fail;
2402  |  
2403  |  /* open each RTP stream */
2404  |  for (i = 0; i < rt->nb_rtsp_streams; i++) {
2405  |  char namebuf[50];
2406  |         rtsp_st = rt->rtsp_streams[i];
2407  |  
2408  |  if (!(rt->rtsp_flags & RTSP_FLAG_CUSTOM_IO)) {
2409  |             AVDictionary *opts = map_to_opts(rt);
2410  |  
2411  |             err = getnameinfo((struct sockaddr*) &rtsp_st->sdp_ip,
2412  |  sizeof(rtsp_st->sdp_ip),
2413  |                               namebuf, sizeof(namebuf), NULL, 0, NI_NUMERICHOST);
2414  |  if (err) {
2415  |                 av_log(s, AV_LOG_ERROR, "getnameinfo: %s\n", gai_strerror(err));
2416  |                 err = AVERROR(EIO);
2417  |                 av_dict_free(&opts);
2418  |  goto fail;
2419  |             }
2420  |             ff_url_join(url, sizeof(url), "rtp", NULL,
2421  |                         namebuf, rtsp_st->sdp_port,
2422  |  "?localport=%d&ttl=%d&connect=%d&write_to_source=%d",
2423  |                         rtsp_st->sdp_port, rtsp_st->sdp_ttl,
2424  |                         rt->rtsp_flags & RTSP_FLAG_FILTER_SRC ? 1 : 0,
2425  |                         rt->rtsp_flags & RTSP_FLAG_RTCP_TO_SOURCE ? 1 : 0);
2426  |  
2427  |             append_source_addrs(url, sizeof(url), "sources",
2428  |                                 rtsp_st->nb_include_source_addrs,
2429  |                                 rtsp_st->include_source_addrs);
2437  |  
2438  |  if (err < 0) {
2439  |                 err = AVERROR_INVALIDDATA;
2440  |  goto fail;
2441  |             }
2442  |         }
2443  |  if ((err = ff_rtsp_open_transport_ctx(s, rtsp_st)))
2444  |  goto fail;
2445  |     }
2446  |  return 0;
2447  | fail:
2448  |     ff_rtsp_close_streams(s);
2449  |     ff_network_close();
2450  |  return err;
2451  | }
2452  |  
2453  | static int sdp_read_close(AVFormatContext *s)
2454  | {
2455  |     ff_rtsp_close_streams(s);
2456  |     ff_network_close();
2457  |  return 0;
2458  | }
2459  |  
2460  | static const AVClass sdp_demuxer_class = {
2461  |     .class_name     = "SDP demuxer",
2462  |     .item_name      = av_default_item_name,
2463  |     .option         = sdp_options,
2464  |     .version        = LIBAVUTIL_VERSION_INT,
2465  | };
2466  |  
2467  | const AVInputFormat ff_sdp_demuxer = {
2468  |     .name           = "sdp",
2469  |     .long_name      = NULL_IF_CONFIG_SMALL("SDP"),
2470  |     .priv_data_size = sizeof(RTSPState),
2471  |     .read_probe     = sdp_probe,
2472  |     .read_header    = sdp_read_header,
2473  |     .read_packet    = ff_rtsp_fetch_packet,
2474  |     .read_close     = sdp_read_close,
2475  |     .priv_class     = &sdp_demuxer_class,
2476  | };
2477  | #endif /* CONFIG_SDP_DEMUXER */
2478  |  
2479  | #if CONFIG_RTP_DEMUXER
2480  | static int rtp_probe(const AVProbeData *p)
2481  | {
2482  |  if (av_strstart(p->filename, "rtp:", NULL))
2483  |  return AVPROBE_SCORE_MAX;
2484  |  return 0;
2485  | }
2486  |  
2487  | static int rtp_read_header(AVFormatContext *s)
2488  | {
2489  |  uint8_t recvbuf[RTP_MAX_PACKET_LENGTH];
2490  |  char host[500], filters_buf[1000];
2491  |  int ret, port;
2492  |     URLContext* in = NULL;
2493  |  int payload_type;
2494  |     AVCodecParameters *par = NULL;
2495  |  struct sockaddr_storage addr;
2496  |     AVIOContext pb;
2497  |     socklen_t addrlen = sizeof(addr);
2498  |     RTSPState *rt = s->priv_data;
2499  |  const char *p;
2500  |     AVBPrint sdp;
2501  |     AVDictionary *opts = NULL;
2502  |  
2503  |  if (!ff_network_init())
    1Assuming the condition is false→
    2←Taking false branch→
2504  |  return AVERROR(EIO);
2505  |  
2506  |  opts = map_to_opts(rt);
2507  |     ret = ffurl_open_whitelist(&in, s->url, AVIO_FLAG_READ,
2508  |                      &s->interrupt_callback, &opts, s->protocol_whitelist, s->protocol_blacklist, NULL);
2509  |     av_dict_free(&opts);
2510  |  if (ret)
    3←Assuming 'ret' is 0→
    4←Taking false branch→
2511  |  goto fail;
2512  |  
2513  |  while (1) {
    5←Loop condition is true.  Entering loop body→
2514  |  ret = ffurl_read(in, recvbuf, sizeof(recvbuf));
2515  |  if (ret == AVERROR(EAGAIN))
    6←Assuming the condition is false→
    7←Taking false branch→
2516  |  continue;
2517  |  if (ret < 0)
    8←Assuming 'ret' is >= 0→
    9←Taking false branch→
2518  |  goto fail;
2519  |  if (ret < 12) {
    10←Assuming 'ret' is >= 12→
    11←Taking false branch→
2520  |             av_log(s, AV_LOG_WARNING, "Received too short packet\n");
2521  |  continue;
2522  |         }
2523  |  
2524  |  if ((recvbuf[0] & 0xc0) != 0x80) {
    12←Assuming the condition is false→
2525  |             av_log(s, AV_LOG_WARNING, "Unsupported RTP version packet "
2526  |  "received\n");
2527  |  continue;
2528  |         }
2529  |  
2530  |  if (RTP_PT_IS_RTCP(recvbuf[1]))
    13←Taking false branch→
    14←Assuming the condition is false→
2531  |  continue;
2532  |  
2533  |  payload_type = recvbuf[1] & 0x7f;
2534  |  break;
2535  |     }
2536  |  getsockname(ffurl_get_file_handle(in), (struct sockaddr*) &addr, &addrlen);
2537  |     ffurl_closep(&in);
2538  |  
2539  |     par = avcodec_parameters_alloc();
2540  |  if (!par) {
    15←Assuming 'par' is non-null→
    16←Taking false branch→
2541  |         ret = AVERROR(ENOMEM);
2542  |  goto fail;
2543  |     }
2544  |  
2545  |  if (ff_rtp_get_codec_info(par, payload_type)) {
    17←Assuming the condition is false→
    18←Taking false branch→
2546  |         av_log(s, AV_LOG_ERROR, "Unable to receive RTP payload type %d "
2547  |  "without an SDP file describing it\n",
2548  |                                  payload_type);
2549  |         ret = AVERROR_INVALIDDATA;
2550  |  goto fail;
2551  |     }
2552  |  if (par->codec_type != AVMEDIA_TYPE_DATA) {
    19←Assuming field 'codec_type' is equal to AVMEDIA_TYPE_DATA→
    20←Taking false branch→
2553  |         av_log(s, AV_LOG_WARNING, "Guessing on RTP content - if not received "
2554  |  "properly you need an SDP file "
2555  |  "describing it\n");
2556  |     }
2557  |  
2558  |  av_url_split(NULL, 0, NULL, 0, host, sizeof(host), &port,
2559  |  NULL, 0, s->url);
2560  |  
2561  |     av_bprint_init(&sdp, 0, AV_BPRINT_SIZE_UNLIMITED);
2562  |  av_bprintf(&sdp, "v=0\r\nc=IN IP%d %s\r\n",
2563  |  addr.ss_family == AF_INET ? 4 : 6, host);
    21←Assuming field 'ss_family' is not equal to AF_INET→
    22←'?' condition is false→
2564  |  
2565  |  p = strchr(s->url, '?');
2566  |  if (p) {
    23←Assuming 'p' is null→
    24←Taking false branch→
2567  |  static const char filters[][2][8] = { { "sources", "incl" },
2568  |                                               { "block",   "excl" } };
2569  |  int i;
2570  |  char *q;
2571  |  for (i = 0; i < FF_ARRAY_ELEMS(filters); i++) {
2572  |  if (av_find_info_tag(filters_buf, sizeof(filters_buf), filters[i][0], p)) {
2573  |                 q = filters_buf;
2574  |  while ((q = strchr(q, ',')) != NULL)
2575  |                     *q = ' ';
2576  |                 av_bprintf(&sdp, "a=source-filter:%s IN IP%d %s %s\r\n",
2577  |                            filters[i][1],
2578  |                            addr.ss_family == AF_INET ? 4 : 6, host,
2579  |                            filters_buf);
2580  |             }
2581  |         }
2582  |     }
2583  |  
2584  |  av_bprintf(&sdp, "m=%s %d RTP/AVP %d\r\n",
2585  |  par->codec_type24.1Field 'codec_type' is equal to AVMEDIA_TYPE_DATA == AVMEDIA_TYPE_DATA  ? "application" :
    25←'?' condition is true→
2586  |                par->codec_type == AVMEDIA_TYPE_VIDEO ? "video" : "audio",
2587  |  port, payload_type);
2588  |  av_log(s, AV_LOG_VERBOSE, "SDP:\n%s\n", sdp.str);
2589  |  if (!av_bprint_is_complete(&sdp))
    26←Taking false branch→
2590  |  goto fail_nobuf;
2591  |  avcodec_parameters_free(&par);
2592  |  
2593  |     ffio_init_context(&pb, sdp.str, sdp.len, 0, NULL, NULL, NULL, NULL);
2594  |     s->pb = &pb;
2595  |  
2596  |  /* if sdp_read_header() fails then following ff_network_close() cancels out */
2597  |  /* ff_network_init() at the start of this function. Otherwise it cancels out */
2598  |  /* ff_network_init() inside sdp_read_header() */
2599  |     ff_network_close();
2600  |  
2601  |     rt->media_type_mask = (1 << (AVMEDIA_TYPE_SUBTITLE+1)) - 1;
2602  |  
2603  |  ret = sdp_read_header(s);
    27←Calling 'sdp_read_header'→
2604  |     s->pb = NULL;
2605  |     av_bprint_finalize(&sdp, NULL);
2606  |  return ret;
2607  |  
2608  | fail_nobuf:
2609  |     ret = AVERROR(ENOMEM);
2610  |     av_log(s, AV_LOG_ERROR, "rtp_read_header(): not enough buffer space for sdp-headers\n");
2611  |     av_bprint_finalize(&sdp, NULL);
2612  | fail:
2613  |     avcodec_parameters_free(&par);
2614  |     ffurl_closep(&in);
2615  |     ff_network_close();
2616  |  return ret;
2617  | }
2618  |  
2619  | static const AVClass rtp_demuxer_class = {
2620  |     .class_name     = "RTP demuxer",
2621  |     .item_name      = av_default_item_name,
2622  |     .option         = rtp_options,
2623  |     .version        = LIBAVUTIL_VERSION_INT,
2624  | };
2625  |  
2626  | const AVInputFormat ff_rtp_demuxer = {
2627  |     .name           = "rtp",
2628  |     .long_name      = NULL_IF_CONFIG_SMALL("RTP input"),
2629  |     .priv_data_size = sizeof(RTSPState),
2630  |     .read_probe     = rtp_probe,
2631  |     .read_header    = rtp_read_header,
2632  |     .read_packet    = ff_rtsp_fetch_packet,
2633  |     .read_close     = sdp_read_close,