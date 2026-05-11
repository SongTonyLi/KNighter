### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/hls.c  
---|---  
Warning:| line 1308, column 20  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


574   |         *dest     =        info->defaultr;
575   |         *dest_len = sizeof(info->defaultr);
576   |     } else if (!strncmp(key, "FORCED=", key_len)) {
577   |         *dest     =        info->forced;
578   |         *dest_len = sizeof(info->forced);
579   |     } else if (!strncmp(key, "CHARACTERISTICS=", key_len)) {
580   |         *dest     =        info->characteristics;
581   |         *dest_len = sizeof(info->characteristics);
582   |     }
583   |  /*
584   |  * ignored:
585   |  * - AUTOSELECT: client may autoselect based on e.g. system language
586   |  * - INSTREAM-ID: EIA-608 closed caption number ("CC1".."CC4")
587   |  */
588   | }
589   |  
590   | /* used by parse_playlist to allocate a new variant+playlist when the
591   |  * playlist is detected to be a Media Playlist (not Master Playlist)
592   |  * and we have no parent Master Playlist (parsing of which would have
593   |  * allocated the variant and playlist already)
594   |  * *pls == NULL  => Master Playlist or parentless Media Playlist
595   |  * *pls != NULL => parented Media Playlist, playlist+variant allocated */
596   | static int ensure_playlist(HLSContext *c, struct playlist **pls, const char *url)
597   | {
598   |  if (*pls)
599   |  return 0;
600   |  if (!new_variant(c, NULL, url, NULL))
601   |  return AVERROR(ENOMEM);
602   |     *pls = c->playlists[c->n_playlists - 1];
603   |  return 0;
604   | }
605   |  
606   | static int open_url_keepalive(AVFormatContext *s, AVIOContext **pb,
607   |  const char *url, AVDictionary **options)
608   | {
609   | #if !CONFIG_HTTP_PROTOCOL
610   |  return AVERROR_PROTOCOL_NOT_FOUND;
611   | #else
612   |  int ret;
613   |     URLContext *uc = ffio_geturlcontext(*pb);
614   |  av_assert0(uc);
615   |     (*pb)->eof_reached = 0;
616   |     ret = ff_http_do_new_request2(uc, url, options);
617   |  if (ret < 0) {
618   |         ff_format_io_close(s, pb);
619   |     }
620   |  return ret;
621   | #endif
622   | }
623   |  
624   | static int open_url(AVFormatContext *s, AVIOContext **pb, const char *url,
625   |                     AVDictionary **opts, AVDictionary *opts2, int *is_http_out)
626   | {
627   |     HLSContext *c = s->priv_data;
628   |     AVDictionary *tmp = NULL;
629   |  const char *proto_name = NULL;
630   |  int ret;
631   |  int is_http = 0;
632   |  
633   |  if (av_strstart(url, "crypto", NULL)) {
634   |  if (url[6] == '+' || url[6] == ':')
635   |             proto_name = avio_find_protocol_name(url + 7);
636   |     } else if (av_strstart(url, "data", NULL)) {
637   |  if (url[4] == '+' || url[4] == ':')
638   |             proto_name = avio_find_protocol_name(url + 5);
639   |     }
640   |  
641   |  if (!proto_name)
642   |         proto_name = avio_find_protocol_name(url);
643   |  
644   |  if (!proto_name)
645   |  return AVERROR_INVALIDDATA;
646   |  
647   |  // only http(s) & file are allowed
648   |  if (av_strstart(proto_name, "file", NULL)) {
649   |  if (strcmp(c->allowed_extensions, "ALL") && !av_match_ext(url, c->allowed_extensions)) {
650   |             av_log(s, AV_LOG_ERROR,
651   |  "Filename extension of \'%s\' is not a common multimedia extension, blocked for security reasons.\n"
652   |  "If you wish to override this adjust allowed_extensions, you can set it to \'ALL\' to allow all\n",
653   |                 url);
654   |  return AVERROR_INVALIDDATA;
655   |         }
656   |     } else if (av_strstart(proto_name, "http", NULL)) {
657   |         is_http = 1;
658   |     } else if (av_strstart(proto_name, "data", NULL)) {
659   |         ;
660   |     } else
661   |  return AVERROR_INVALIDDATA;
662   |  
663   |  if (!strncmp(proto_name, url, strlen(proto_name)) && url[strlen(proto_name)] == ':')
664   |         ;
665   |  else if (av_strstart(url, "crypto", NULL) && !strncmp(proto_name, url + 7, strlen(proto_name)) && url[7 + strlen(proto_name)] == ':')
666   |         ;
667   |  else if (av_strstart(url, "data", NULL) && !strncmp(proto_name, url + 5, strlen(proto_name)) && url[5 + strlen(proto_name)] == ':')
668   |         ;
669   |  else if (strcmp(proto_name, "file") || !strncmp(url, "file,", 5))
670   |  return AVERROR_INVALIDDATA;
671   |  
672   |     av_dict_copy(&tmp, *opts, 0);
673   |     av_dict_copy(&tmp, opts2, 0);
674   |  
675   |  if (is_http && c->http_persistent && *pb) {
676   |         ret = open_url_keepalive(c->ctx, pb, url, &tmp);
677   |  if (ret == AVERROR_EXIT) {
678   |             av_dict_free(&tmp);
679   |  return ret;
680   |         } else if (ret < 0) {
681   |  if (ret != AVERROR_EOF)
682   |                 av_log(s, AV_LOG_WARNING,
683   |  "keepalive request failed for '%s' with error: '%s' when opening url, retrying with new connection\n",
684   |                     url, av_err2str(ret));
685   |             av_dict_copy(&tmp, *opts, 0);
686   |             av_dict_copy(&tmp, opts2, 0);
687   |             ret = s->io_open(s, pb, url, AVIO_FLAG_READ, &tmp);
688   |         }
689   |     } else {
690   |         ret = s->io_open(s, pb, url, AVIO_FLAG_READ, &tmp);
691   |     }
692   |  if (ret >= 0) {
693   |  // update cookies on http response with setcookies.
694   |  char *new_cookies = NULL;
695   |  
696   |  if (!(s->flags & AVFMT_FLAG_CUSTOM_IO))
697   |             av_opt_get(*pb, "cookies", AV_OPT_SEARCH_CHILDREN, (uint8_t**)&new_cookies);
698   |  
699   |  if (new_cookies)
700   |             av_dict_set(opts, "cookies", new_cookies, AV_DICT_DONT_STRDUP_VAL);
701   |     }
702   |  
703   |     av_dict_free(&tmp);
704   |  
705   |  if (is_http_out)
706   |         *is_http_out = is_http;
707   |  
708   |  return ret;
709   | }
710   |  
711   | static int parse_playlist(HLSContext *c, const char *url,
712   |  struct playlist *pls, AVIOContext *in)
713   | {
714   |  int ret = 0, is_segment = 0, is_variant = 0;
715   |     int64_t duration = 0;
716   |  enum KeyType key_type = KEY_NONE;
717   |     uint8_t iv[16] = "";
718   |  int has_iv = 0;
719   |  char key[MAX_URL_SIZE] = "";
720   |  char line[MAX_URL_SIZE];
721   |  const char *ptr;
722   |  int close_in = 0;
723   |     int64_t seg_offset = 0;
724   |     int64_t seg_size = -1;
725   |     uint8_t *new_url = NULL;
726   |  struct variant_info variant_info;
727   |  char tmp_str[MAX_URL_SIZE];
728   |  struct segment *cur_init_section = NULL;
729   |  int is_http = av_strstart(url, "http", NULL);
730   |  struct segment **prev_segments = NULL;
731   |  int prev_n_segments = 0;
732   |     int64_t prev_start_seq_no = -1;
733   |  
734   |  if (is_http && !in && c->http_persistent && c->playlist_pb) {
735   |         in = c->playlist_pb;
736   |         ret = open_url_keepalive(c->ctx, &c->playlist_pb, url, NULL);
737   |  if (ret == AVERROR_EXIT) {
738   |  return ret;
956   |                 is_segment = 0;
957   |  
958   |                 seg->size = seg_size;
959   |  if (seg_size >= 0) {
960   |                     seg->url_offset = seg_offset;
961   |                     seg_offset += seg_size;
962   |                     seg_size = -1;
963   |                 } else {
964   |                     seg->url_offset = 0;
965   |                     seg_offset = 0;
966   |                 }
967   |  
968   |                 seg->init_section = cur_init_section;
969   |             }
970   |         }
971   |     }
972   |  if (prev_segments) {
973   |  if (pls->start_seq_no > prev_start_seq_no && c->first_timestamp != AV_NOPTS_VALUE) {
974   |             int64_t prev_timestamp = c->first_timestamp;
975   |  int i;
976   |             int64_t diff = pls->start_seq_no - prev_start_seq_no;
977   |  for (i = 0; i < prev_n_segments && i < diff; i++) {
978   |                 c->first_timestamp += prev_segments[i]->duration;
979   |             }
980   |             av_log(c->ctx, AV_LOG_DEBUG, "Media sequence change (%"PRId64" -> %"PRId64")"
981   |  " reflected in first_timestamp: %"PRId64" -> %"PRId64"\n",
982   |                    prev_start_seq_no, pls->start_seq_no,
983   |                    prev_timestamp, c->first_timestamp);
984   |         } else if (pls->start_seq_no < prev_start_seq_no) {
985   |             av_log(c->ctx, AV_LOG_WARNING, "Media sequence changed unexpectedly: %"PRId64" -> %"PRId64"\n",
986   |                    prev_start_seq_no, pls->start_seq_no);
987   |         }
988   |         free_segment_dynarray(prev_segments, prev_n_segments);
989   |         av_freep(&prev_segments);
990   |     }
991   |  if (pls)
992   |         pls->last_load_time = av_gettime_relative();
993   |  
994   | fail:
995   |     av_free(new_url);
996   |  if (close_in)
997   |         ff_format_io_close(c->ctx, &in);
998   |     c->ctx->ctx_flags = c->ctx->ctx_flags & ~(unsigned)AVFMTCTX_UNSEEKABLE;
999   |  if (!c->n_variants || !c->variants[0]->n_playlists ||
1000  |         !(c->variants[0]->playlists[0]->finished ||
1001  |           c->variants[0]->playlists[0]->type == PLS_TYPE_EVENT))
1002  |         c->ctx->ctx_flags |= AVFMTCTX_UNSEEKABLE;
1003  |  return ret;
1004  | }
1005  |  
1006  | static struct segment *current_segment(struct playlist *pls)
1007  | {
1008  |  return pls->segments[pls->cur_seq_no - pls->start_seq_no];
1009  | }
1010  |  
1011  | static struct segment *next_segment(struct playlist *pls)
1012  | {
1013  |     int64_t n = pls->cur_seq_no - pls->start_seq_no + 1;
1014  |  if (n >= pls->n_segments)
1015  |  return NULL;
1016  |  return pls->segments[n];
1017  | }
1018  |  
1019  | static int read_from_url(struct playlist *pls, struct segment *seg,
1020  |                          uint8_t *buf, int buf_size)
1021  | {
1022  |  int ret;
1023  |  
1024  |  /* limit read if the segment was only a part of a file */
1025  |  if (seg->size >= 0)
1026  |         buf_size = FFMIN(buf_size, seg->size - pls->cur_seg_offset);
1027  |  
1028  |     ret = avio_read(pls->input, buf, buf_size);
1029  |  if (ret > 0)
1030  |         pls->cur_seg_offset += ret;
1031  |  
1032  |  return ret;
1033  | }
1034  |  
1035  | /* Parse the raw ID3 data and pass contents to caller */
1036  | static void parse_id3(AVFormatContext *s, AVIOContext *pb,
1037  |                       AVDictionary **metadata, int64_t *dts,
1038  |                       ID3v2ExtraMetaAPIC **apic, ID3v2ExtraMeta **extra_meta)
1183  |  * both of those cases together with the possibility for multiple
1184  |  * tags would make the handling a bit complex.
1185  |  */
1186  |             pls->id3_buf = av_fast_realloc(pls->id3_buf, &pls->id3_buf_size, id3_buf_pos + taglen);
1187  |  if (!pls->id3_buf)
1188  |  break;
1189  |             memcpy(pls->id3_buf + id3_buf_pos, buf, tag_got_bytes);
1190  |             id3_buf_pos += tag_got_bytes;
1191  |  
1192  |  /* strip the intercepted bytes */
1193  |             *len -= tag_got_bytes;
1194  |             memmove(buf, buf + tag_got_bytes, *len);
1195  |             av_log(pls->parent, AV_LOG_DEBUG, "Stripped %d HLS ID3 bytes\n", tag_got_bytes);
1196  |  
1197  |  if (remaining > 0) {
1198  |  /* read the rest of the tag in */
1199  |  if (read_from_url(pls, seg, pls->id3_buf + id3_buf_pos, remaining) != remaining)
1200  |  break;
1201  |                 id3_buf_pos += remaining;
1202  |                 av_log(pls->parent, AV_LOG_DEBUG, "Stripped additional %d HLS ID3 bytes\n", remaining);
1203  |             }
1204  |  
1205  |         } else {
1206  |  /* no more ID3 tags */
1207  |  break;
1208  |         }
1209  |     }
1210  |  
1211  |  /* re-fill buffer for the caller unless EOF */
1212  |  if (*len >= 0 && (fill_buf || *len == 0)) {
1213  |         bytes = read_from_url(pls, seg, buf + *len, buf_size - *len);
1214  |  
1215  |  /* ignore error if we already had some data */
1216  |  if (bytes >= 0)
1217  |             *len += bytes;
1218  |  else if (*len == 0)
1219  |             *len = bytes;
1220  |     }
1221  |  
1222  |  if (pls->id3_buf) {
1223  |  /* Now parse all the ID3 tags */
1224  |         AVIOContext id3ioctx;
1225  |         ffio_init_context(&id3ioctx, pls->id3_buf, id3_buf_pos, 0, NULL, NULL, NULL, NULL);
1226  |         handle_id3(&id3ioctx, pls);
1227  |     }
1228  |  
1229  |  if (pls->is_id3_timestamped == -1)
1230  |         pls->is_id3_timestamped = (pls->id3_mpegts_timestamp != AV_NOPTS_VALUE);
1231  | }
1232  |  
1233  | static int open_input(HLSContext *c, struct playlist *pls, struct segment *seg, AVIOContext **in)
1234  | {
1235  |  AVDictionary *opts = NULL;
1236  |  int ret;
1237  |  int is_http = 0;
1238  |  
1239  |  if (c->http_persistent)
    24←Assuming field 'http_persistent' is 0→
    25←Taking false branch→
1240  |         av_dict_set(&opts, "multiple_requests", "1", 0);
1241  |  
1242  |  if (seg->size >= 0) {
    26←Assuming field 'size' is < 0→
    27←Taking false branch→
1243  |  /* try to restrict the HTTP request to the part we want
1244  |  * (if this is in fact a HTTP request) */
1245  |         av_dict_set_int(&opts, "offset", seg->url_offset, 0);
1246  |         av_dict_set_int(&opts, "end_offset", seg->url_offset + seg->size, 0);
1247  |     }
1248  |  
1249  |  av_log(pls->parent, AV_LOG_VERBOSE, "HLS request for url '%s', offset %"PRId64", playlist %d\n",
1250  |            seg->url, seg->url_offset, pls->index);
1251  |  
1252  |  if (seg->key_type == KEY_NONE) {
    28←Assuming field 'key_type' is not equal to KEY_NONE→
    29←Taking false branch→
1253  |         ret = open_url(pls->parent, in, seg->url, &c->avio_opts, opts, &is_http);
1254  |     } else if (seg->key_type == KEY_AES_128) {
    30←Assuming field 'key_type' is equal to KEY_AES_128→
    31←Taking true branch→
1255  |  char iv[33], key[33], url[MAX_URL_SIZE];
1256  |  if (strcmp(seg->key, pls->key_url)) {
    32←Assuming the condition is true→
    33←Taking true branch→
1257  |  AVIOContext *pb = NULL;
1258  |  if (open_url(pls->parent, &pb, seg->key, &c->avio_opts, opts, NULL) == 0) {
    34←Assuming the condition is true→
    35←Taking true branch→
1259  |  ret = avio_read(pb, pls->key, sizeof(pls->key));
1260  |  if (ret != sizeof(pls->key)) {
    36←Assuming the condition is false→
    37←Taking false branch→
1261  |                     av_log(pls->parent, AV_LOG_ERROR, "Unable to read key file %s\n",
1262  |                            seg->key);
1263  |                 }
1264  |  ff_format_io_close(pls->parent, &pb);
1265  |             } else {
1266  |                 av_log(pls->parent, AV_LOG_ERROR, "Unable to open key file %s\n",
1267  |                        seg->key);
1268  |             }
1269  |  av_strlcpy(pls->key_url, seg->key, sizeof(pls->key_url));
1270  |         }
1271  |  ff_data_to_hex(iv, seg->iv, sizeof(seg->iv), 0);
1272  |         ff_data_to_hex(key, pls->key, sizeof(pls->key), 0);
1273  |         iv[32] = key[32] = '\0';
1274  |  if (strstr(seg->url, "://"))
    38←Assuming the condition is true→
    39←Taking true branch→
1275  |  snprintf(url, sizeof(url), "crypto+%s", seg->url);
1276  |  else
1277  |             snprintf(url, sizeof(url), "crypto:%s", seg->url);
1278  |  
1279  |  av_dict_set(&opts, "key", key, 0);
1280  |         av_dict_set(&opts, "iv", iv, 0);
1281  |  
1282  |         ret = open_url(pls->parent, in, url, &c->avio_opts, opts, &is_http);
1283  |  if (ret < 0) {
    40←Assuming 'ret' is >= 0→
    41←Taking false branch→
1284  |  goto cleanup;
1285  |         }
1286  |  ret = 0;
1287  |     } else if (seg->key_type == KEY_SAMPLE_AES) {
1288  |         av_log(pls->parent, AV_LOG_ERROR,
1289  |  "SAMPLE-AES encryption is not supported yet\n");
1290  |         ret = AVERROR_PATCHWELCOME;
1291  |     }
1292  |  else
1293  |       ret = AVERROR(ENOSYS);
1294  |  
1295  |  /* Seek to the requested position. If this was a HTTP request, the offset
1296  |  * should already be where want it to, but this allows e.g. local testing
1297  |  * without a HTTP server.
1298  |  *
1299  |  * This is not done for HTTP at all as avio_seek() does internal bookkeeping
1300  |  * of file offset which is out-of-sync with the actual offset when "offset"
1301  |  * AVOption is used with http protocol, causing the seek to not be a no-op
1302  |  * as would be expected. Wrong offset received from the server will not be
1303  |  * noticed without the call, though.
1304  |  */
1305  |  if (ret41.1'ret' is equal to 0 == 0 && !is_http && seg->url_offset) {
    42←Assuming 'is_http' is 0→
    43←Assuming field 'url_offset' is not equal to 0→
    44←Taking true branch→
1306  |  int64_t seekret = avio_seek(*in, seg->url_offset, SEEK_SET);
1307  |  if (seekret < 0) {
    45←Assuming 'seekret' is < 0→
    46←Taking true branch→
1308  |  av_log(pls->parent, AV_LOG_ERROR, "Unable to seek to offset %"PRId64" of HLS segment '%s'\n", seg->url_offset, seg->url);
    47←buffer read by avio_read may be partially uninitialized
1309  |             ret = seekret;
1310  |             ff_format_io_close(pls->parent, in);
1311  |         }
1312  |     }
1313  |  
1314  | cleanup:
1315  |     av_dict_free(&opts);
1316  |     pls->cur_seg_offset = 0;
1317  |  return ret;
1318  | }
1319  |  
1320  | static int update_init_section(struct playlist *pls, struct segment *seg)
1321  | {
1322  |  static const int max_init_section_size = 1024*1024;
1323  |     HLSContext *c = pls->parent->priv_data;
1324  |     int64_t sec_size;
1325  |     int64_t urlsize;
1326  |  int ret;
1327  |  
1328  |  if (seg->init_section == pls->cur_init_section)
    19←Assuming field 'init_section' is not equal to field 'cur_init_section'→
    20←Taking false branch→
1329  |  return 0;
1330  |  
1331  |  pls->cur_init_section = NULL;
1332  |  
1333  |  if (!seg->init_section)
    21←Assuming field 'init_section' is non-null→
    22←Taking false branch→
1334  |  return 0;
1335  |  
1336  |  ret = open_input(c, pls, seg->init_section, &pls->input);
    23←Calling 'open_input'→
1337  |  if (ret < 0) {
1338  |         av_log(pls->parent, AV_LOG_WARNING,
1339  |  "Failed to open an initialization section in playlist %d\n",
1340  |                pls->index);
1341  |  return ret;
1342  |     }
1343  |  
1344  |  if (seg->init_section->size >= 0)
1345  |         sec_size = seg->init_section->size;
1346  |  else if ((urlsize = avio_size(pls->input)) >= 0)
1347  |         sec_size = urlsize;
1348  |  else
1349  |         sec_size = max_init_section_size;
1350  |  
1351  |     av_log(pls->parent, AV_LOG_DEBUG,
1352  |  "Downloading an initialization section of size %"PRId64"\n",
1353  |            sec_size);
1354  |  
1355  |     sec_size = FFMIN(sec_size, max_init_section_size);
1356  |  
1357  |     av_fast_malloc(&pls->init_sec_buf, &pls->init_sec_buf_size, sec_size);
1358  |  
1359  |     ret = read_from_url(pls, seg->init_section, pls->init_sec_buf,
1360  |                         pls->init_sec_buf_size);
1361  |     ff_format_io_close(pls->parent, &pls->input);
1362  |  
1363  |  if (ret < 0)
1364  |  return ret;
1365  |  
1366  |     pls->cur_init_section = seg->init_section;
1367  |     pls->init_sec_data_len = ret;
1368  |     pls->init_sec_buf_read_offset = 0;
1369  |  
1370  |  /* spec says audio elementary streams do not have media initialization
1371  |  * sections, so there should be no ID3 timestamps */
1372  |     pls->is_id3_timestamped = 0;
1373  |  
1374  |  return 0;
1375  | }
1376  |  
1377  | static int64_t default_reload_interval(struct playlist *pls)
1378  | {
1379  |  return pls->n_segments > 0 ?
1380  |                           pls->segments[pls->n_segments - 1]->duration :
1381  |                           pls->target_duration;
1382  | }
1383  |  
1384  | static int playlist_needed(struct playlist *pls)
1385  | {
1386  |     AVFormatContext *s = pls->parent;
1387  |  int i, j;
1388  |  int stream_needed = 0;
1389  |  int first_st;
1390  |  
1391  |  /* If there is no context or streams yet, the playlist is needed */
1392  |  if (!pls->ctx || !pls->n_main_streams)
1393  |  return 1;
1394  |  
1395  |  /* check if any of the streams in the playlist are needed */
1396  |  for (i = 0; i < pls->n_main_streams; i++) {
1397  |  if (pls->main_streams[i]->discard < AVDISCARD_ALL) {
1398  |             stream_needed = 1;
1399  |  break;
1400  |         }
1401  |     }
1402  |  
1403  |  /* If all streams in the playlist were discarded, the playlist is not
1404  |  * needed (regardless of whether whole programs are discarded or not). */
1405  |  if (!stream_needed)
1406  |  return 0;
1407  |  
1408  |  /* Otherwise, check if all the programs (variants) this playlist is in are
1409  |  * discarded. Since all streams in the playlist are part of the same programs
1410  |  * we can just check the programs of the first stream. */
1411  |  
1412  |     first_st = pls->main_streams[0]->index;
1413  |  
1414  |  for (i = 0; i < s->nb_programs; i++) {
1415  |         AVProgram *program = s->programs[i];
1416  |  if (program->discard < AVDISCARD_ALL) {
1417  |  for (j = 0; j < program->nb_stream_indexes; j++) {
1418  |  if (program->stream_index[j] == first_st) {
1419  |  /* playlist is in an undiscarded program */
1420  |  return 1;
1421  |                 }
1422  |             }
1423  |         }
1424  |     }
1425  |  
1426  |  /* some streams were not discarded but all the programs were */
1427  |  return 0;
1428  | }
1429  |  
1430  | static int read_data(void *opaque, uint8_t *buf, int buf_size)
1431  | {
1432  |  struct playlist *v = opaque;
1433  |     HLSContext *c = v->parent->priv_data;
1434  |  int ret;
1435  |  int just_opened = 0;
1436  |  int reload_count = 0;
1437  |  struct segment *seg;
1438  |  
1439  | restart:
1440  |  if (!v->needed)
    1Assuming field 'needed' is not equal to 0→
1441  |  return AVERROR_EOF;
1442  |  
1443  |  if (!v->input || (c->http_persistent && v->input_read_done)) {
    2←Assuming field 'input' is non-null→
    3←Assuming field 'http_persistent' is not equal to 0→
    4←Assuming field 'input_read_done' is not equal to 0→
    5←Taking true branch→
1444  |  int64_t reload_interval;
1445  |  
1446  |  /* Check that the playlist is still needed before opening a new
1447  |  * segment. */
1448  |         v->needed = playlist_needed(v);
1449  |  
1450  |  if (!v->needed) {
    6←Assuming field 'needed' is not equal to 0→
    7←Taking false branch→
1451  |             av_log(v->parent, AV_LOG_INFO, "No longer receiving playlist %d ('%s')\n",
1452  |                    v->index, v->url);
1453  |  return AVERROR_EOF;
1454  |         }
1455  |  
1456  |  /* If this is a live stream and the reload interval has elapsed since
1457  |  * the last playlist reload, reload the playlists now. */
1458  |  reload_interval = default_reload_interval(v);
1459  |  
1460  | reload:
1461  |         reload_count++;
1462  |  if (reload_count > c->max_reload)
    8←Assuming 'reload_count' is <= field 'max_reload'→
1463  |  return AVERROR_EOF;
1464  |  if (!v->finished &&
    9←Assuming field 'finished' is not equal to 0→
1465  |             av_gettime_relative() - v->last_load_time >= reload_interval) {
1466  |  if ((ret = parse_playlist(c, v->url, v, NULL)) < 0) {
1467  |  if (ret != AVERROR_EXIT)
1468  |                     av_log(v->parent, AV_LOG_WARNING, "Failed to reload playlist %d\n",
1469  |                            v->index);
1470  |  return ret;
1471  |             }
1472  |  /* If we need to reload the playlist again below (if
1473  |  * there's still no more segments), switch to a reload
1474  |  * interval of half the target duration. */
1475  |             reload_interval = v->target_duration / 2;
1476  |         }
1477  |  if (v->cur_seq_no < v->start_seq_no) {
    10←Assuming field 'cur_seq_no' is >= field 'start_seq_no'→
    11←Taking false branch→
1478  |             av_log(v->parent, AV_LOG_WARNING,
1479  |  "skipping %"PRId64" segments ahead, expired from playlists\n",
1480  |                    v->start_seq_no - v->cur_seq_no);
1481  |             v->cur_seq_no = v->start_seq_no;
1482  |         }
1483  |  if (v->cur_seq_no > v->last_seq_no) {
    12←Assuming field 'cur_seq_no' is <= field 'last_seq_no'→
    13←Taking false branch→
1484  |             v->last_seq_no = v->cur_seq_no;
1485  |             v->m3u8_hold_counters = 0;
1486  |         } else if (v->last_seq_no == v->cur_seq_no) {
    14←Assuming field 'last_seq_no' is not equal to field 'cur_seq_no'→
    15←Taking false branch→
1487  |             v->m3u8_hold_counters++;
1488  |  if (v->m3u8_hold_counters >= c->m3u8_hold_counters) {
1489  |  return AVERROR_EOF;
1490  |             }
1491  |         } else {
1492  |  av_log(v->parent, AV_LOG_WARNING, "maybe the m3u8 list sequence have been wraped.\n");
1493  |         }
1494  |  if (v->cur_seq_no >= v->start_seq_no + v->n_segments) {
    16←Assuming the condition is false→
    17←Taking false branch→
1495  |  if (v->finished)
1496  |  return AVERROR_EOF;
1497  |  while (av_gettime_relative() - v->last_load_time < reload_interval) {
1498  |  if (ff_check_interrupt(c->interrupt_callback))
1499  |  return AVERROR_EXIT;
1500  |                 av_usleep(100*1000);
1501  |             }
1502  |  /* Enough time has elapsed since the last reload */
1503  |  goto reload;
1504  |         }
1505  |  
1506  |  v->input_read_done = 0;
1507  |         seg = current_segment(v);
1508  |  
1509  |  /* load/update Media Initialization Section, if any */
1510  |  ret = update_init_section(v, seg);
    18←Calling 'update_init_section'→
1511  |  if (ret)
1512  |  return ret;
1513  |  
1514  |  if (c->http_multiple == 1 && v->input_next_requested) {
1515  |  FFSWAP(AVIOContext *, v->input, v->input_next);
1516  |             v->cur_seg_offset = 0;
1517  |             v->input_next_requested = 0;
1518  |             ret = 0;
1519  |         } else {
1520  |             ret = open_input(c, v, seg, &v->input);
1521  |         }
1522  |  if (ret < 0) {
1523  |  if (ff_check_interrupt(c->interrupt_callback))
1524  |  return AVERROR_EXIT;
1525  |             av_log(v->parent, AV_LOG_WARNING, "Failed to open segment %"PRId64" of playlist %d\n",
1526  |                    v->cur_seq_no,
1527  |                    v->index);
1528  |             v->cur_seq_no += 1;
1529  |  goto reload;
1530  |         }
1531  |         just_opened = 1;
1532  |     }
1533  |  
1534  |  if (c->http_multiple == -1) {
1535  |         uint8_t *http_version_opt = NULL;
1536  |  int r = av_opt_get(v->input, "http_version", AV_OPT_SEARCH_CHILDREN, &http_version_opt);
1537  |  if (r >= 0) {
1538  |             c->http_multiple = (!strncmp((const char *)http_version_opt, "1.1", 3) || !strncmp((const char *)http_version_opt, "2.0", 3));
1539  |             av_freep(&http_version_opt);
1540  |         }