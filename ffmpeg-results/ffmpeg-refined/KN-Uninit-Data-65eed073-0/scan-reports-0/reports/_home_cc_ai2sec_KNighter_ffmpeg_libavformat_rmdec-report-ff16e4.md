### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/rmdec.c  
---|---  
Warning:| line 201, column 36  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


57    | };
58    |  
59    | typedef struct RMDemuxContext {
60    |  int nb_packets;
61    |  int old_format;
62    |  int current_stream;
63    |  int remaining_len;
64    |  int audio_stream_num; ///< Stream number for audio packets
65    |  int audio_pkt_cnt; ///< Output packet counter
66    |  int data_end;
67    | } RMDemuxContext;
68    |  
69    | static int rm_read_close(AVFormatContext *s);
70    |  
71    | static inline void get_strl(AVIOContext *pb, char *buf, int buf_size, int len)
72    | {
73    |  int read = avio_get_str(pb, len, buf, buf_size);
74    |  
75    |  if (read > 0)
76    |         avio_skip(pb, len - read);
77    | }
78    |  
79    | static void get_str8(AVIOContext *pb, char *buf, int buf_size)
80    | {
81    |     get_strl(pb, buf, buf_size, avio_r8(pb));
82    | }
83    |  
84    | static int rm_read_extradata(AVFormatContext *s, AVIOContext *pb, AVCodecParameters *par, unsigned size)
85    | {
86    |  if (size >= 1<<24) {
87    |         av_log(s, AV_LOG_ERROR, "extradata size %u too large\n", size);
88    |  return -1;
89    |     }
90    |  return ff_get_extradata(s, par, pb, size);
91    | }
92    |  
93    | static void rm_read_metadata(AVFormatContext *s, AVIOContext *pb, int wide)
94    | {
95    |  char buf[1024];
96    |  int i;
97    |  
98    |  for (i=0; i<FF_ARRAY_ELEMS(ff_rm_metadata); i++) {
99    |  int len = wide ? avio_rb16(pb) : avio_r8(pb);
100   |  if (len > 0) {
101   |             get_strl(pb, buf, sizeof(buf), len);
102   |             av_dict_set(&s->metadata, ff_rm_metadata[i], buf, 0);
103   |         }
104   |     }
105   | }
106   |  
107   | RMStream *ff_rm_alloc_rmstream (void)
108   | {
109   |     RMStream *rms = av_mallocz(sizeof(RMStream));
110   |  if (!rms)
111   |  return NULL;
112   |     rms->curpic_num = -1;
113   |  return rms;
114   | }
115   |  
116   | void ff_rm_free_rmstream (RMStream *rms)
117   | {
118   |  if (!rms)
119   |  return;
120   |  
121   |     av_packet_unref(&rms->pkt);
122   | }
123   |  
124   | static int rm_read_audio_stream_info(AVFormatContext *s, AVIOContext *pb,
125   |                                      AVStream *st, RMStream *ast, int read_all)
126   | {
127   |  char buf[256];
128   |     uint32_t version;
129   |  int ret;
130   |  
131   |  /* ra type header */
132   |     version = avio_rb16(pb); /* version */
133   |  if (version == 3) {
    48←Assuming 'version' is not equal to 3→
    49←Taking false branch→
134   |  unsigned bytes_per_minute;
135   |  int header_size = avio_rb16(pb);
136   |         int64_t startpos = avio_tell(pb);
137   |         avio_skip(pb, 8);
138   |         bytes_per_minute = avio_rb16(pb);
139   |         avio_skip(pb, 4);
140   |         rm_read_metadata(s, pb, 0);
141   |  if ((startpos + header_size) >= avio_tell(pb) + 2) {
142   |  // fourcc (should always be "lpcJ")
143   |             avio_r8(pb);
144   |             get_str8(pb, buf, sizeof(buf));
145   |         }
146   |  // Skip extra header crap (this should never happen)
147   |  if ((startpos + header_size) > avio_tell(pb))
148   |             avio_skip(pb, header_size + startpos - avio_tell(pb));
149   |  if (bytes_per_minute)
150   |             st->codecpar->bit_rate = 8LL * bytes_per_minute / 60;
151   |         st->codecpar->sample_rate = 8000;
152   |         st->codecpar->channels = 1;
153   |         st->codecpar->channel_layout = AV_CH_LAYOUT_MONO;
154   |         st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
155   |         st->codecpar->codec_id = AV_CODEC_ID_RA_144;
156   |         ast->deint_id = DEINT_ID_INT0;
157   |     } else {
158   |  int flavor, sub_packet_h, coded_framesize, sub_packet_size;
159   |  int codecdata_length;
160   |  unsigned bytes_per_minute;
161   |  /* old version (4) */
162   |         avio_skip(pb, 2); /* unused */
163   |         avio_rb32(pb); /* .ra4 */
164   |         avio_rb32(pb); /* data size */
165   |         avio_rb16(pb); /* version2 */
166   |         avio_rb32(pb); /* header size */
167   |         flavor= avio_rb16(pb); /* add codec info / flavor */
168   |         coded_framesize = avio_rb32(pb); /* coded frame size */
169   |  if (coded_framesize < 0)
    50←Assuming 'coded_framesize' is >= 0→
    51←Taking false branch→
170   |  return AVERROR_INVALIDDATA;
171   |  ast->coded_framesize = coded_framesize;
172   |  
173   |         avio_rb32(pb); /* ??? */
174   |         bytes_per_minute = avio_rb32(pb);
175   |  if (version == 4) {
    52←Assuming 'version' is not equal to 4→
    53←Taking false branch→
176   |  if (bytes_per_minute)
177   |                 st->codecpar->bit_rate = 8LL * bytes_per_minute / 60;
178   |         }
179   |  avio_rb32(pb); /* ??? */
180   |         ast->sub_packet_h = sub_packet_h = avio_rb16(pb); /* 1 */
181   |         st->codecpar->block_align= avio_rb16(pb); /* frame size */
182   |         ast->sub_packet_size = sub_packet_size = avio_rb16(pb); /* sub packet size */
183   |         avio_rb16(pb); /* ??? */
184   |  if (version == 5) {
    54←Assuming 'version' is equal to 5→
    55←Taking true branch→
185   |  avio_rb16(pb); avio_rb16(pb); avio_rb16(pb);
186   |         }
187   |  st->codecpar->sample_rate = avio_rb16(pb);
188   |         avio_rb32(pb);
189   |         st->codecpar->channels = avio_rb16(pb);
190   |  if (version55.1'version' is equal to 5 == 5) {
    56←Taking true branch→
191   |  ast->deint_id = avio_rl32(pb);
192   |             avio_read(pb, buf, 4);
193   |  buf[4] = 0;
194   |         } else {
195   |  AV_WL32(buf, 0);
196   |             get_str8(pb, buf, sizeof(buf)); /* desc */
197   |             ast->deint_id = AV_RL32(buf);
198   |             get_str8(pb, buf, sizeof(buf)); /* desc */
199   |         }
200   |  st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
201   |  st->codecpar->codec_tag  = AV_RL32(buf);
    57←buffer read by avio_read may be partially uninitialized
202   |         st->codecpar->codec_id   = ff_codec_get_id(ff_rm_codec_tags,
203   |                                                    st->codecpar->codec_tag);
204   |  
205   |  switch (st->codecpar->codec_id) {
206   |  case AV_CODEC_ID_AC3:
207   |             st->internal->need_parsing = AVSTREAM_PARSE_FULL;
208   |  break;
209   |  case AV_CODEC_ID_RA_288:
210   |             st->codecpar->extradata_size= 0;
211   |             av_freep(&st->codecpar->extradata);
212   |             ast->audio_framesize = st->codecpar->block_align;
213   |             st->codecpar->block_align = coded_framesize;
214   |  break;
215   |  case AV_CODEC_ID_COOK:
216   |             st->internal->need_parsing = AVSTREAM_PARSE_HEADERS;
217   |  case AV_CODEC_ID_ATRAC3:
218   |  case AV_CODEC_ID_SIPR:
219   |  if (read_all) {
220   |                 codecdata_length = 0;
221   |             } else {
222   |                 avio_rb16(pb); avio_r8(pb);
223   |  if (version == 5)
224   |                     avio_r8(pb);
225   |                 codecdata_length = avio_rb32(pb);
226   |  if((unsigned)codecdata_length > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE){
227   |                     av_log(s, AV_LOG_ERROR, "codecdata_length too large\n");
228   |  return -1;
229   |                 }
230   |             }
231   |  
266   |  break;
267   |         }
268   |  switch (ast->deint_id) {
269   |  case DEINT_ID_INT4:
270   |  if (ast->coded_framesize > ast->audio_framesize ||
271   |                 sub_packet_h <= 1 ||
272   |                 ast->coded_framesize * sub_packet_h > (2 + (sub_packet_h & 1)) * ast->audio_framesize)
273   |  return AVERROR_INVALIDDATA;
274   |  if (ast->coded_framesize * sub_packet_h != 2*ast->audio_framesize) {
275   |                 avpriv_request_sample(s, "mismatching interleaver parameters");
276   |  return AVERROR_INVALIDDATA;
277   |             }
278   |  break;
279   |  case DEINT_ID_GENR:
280   |  if (ast->sub_packet_size <= 0 ||
281   |                 ast->sub_packet_size > ast->audio_framesize)
282   |  return AVERROR_INVALIDDATA;
283   |  if (ast->audio_framesize % ast->sub_packet_size)
284   |  return AVERROR_INVALIDDATA;
285   |  break;
286   |  case DEINT_ID_SIPR:
287   |  case DEINT_ID_INT0:
288   |  case DEINT_ID_VBRS:
289   |  case DEINT_ID_VBRF:
290   |  break;
291   |  default:
292   |             av_log(s, AV_LOG_ERROR ,"Unknown interleaver %"PRIX32"\n", ast->deint_id);
293   |  return AVERROR_INVALIDDATA;
294   |         }
295   |  if (ast->deint_id == DEINT_ID_INT4 ||
296   |             ast->deint_id == DEINT_ID_GENR ||
297   |             ast->deint_id == DEINT_ID_SIPR) {
298   |  if (st->codecpar->block_align <= 0 ||
299   |                 ast->audio_framesize * (uint64_t)sub_packet_h > (unsigned)INT_MAX ||
300   |                 ast->audio_framesize * sub_packet_h < st->codecpar->block_align)
301   |  return AVERROR_INVALIDDATA;
302   |  if (av_new_packet(&ast->pkt, ast->audio_framesize * sub_packet_h) < 0)
303   |  return AVERROR(ENOMEM);
304   |         }
305   |  
306   |  if (read_all) {
307   |             avio_r8(pb);
308   |             avio_r8(pb);
309   |             avio_r8(pb);
310   |             rm_read_metadata(s, pb, 0);
311   |         }
312   |     }
313   |  return 0;
314   | }
315   |  
316   | int ff_rm_read_mdpr_codecdata(AVFormatContext *s, AVIOContext *pb,
317   |                               AVStream *st, RMStream *rst,
318   |  unsigned int codec_data_size, const uint8_t *mime)
319   | {
320   |  unsigned int v;
321   |  int size;
322   |     int64_t codec_pos;
323   |  int ret;
324   |  
325   |  if (codec_data_size > INT_MAX)
    41←Assuming 'codec_data_size' is <= INT_MAX→
    42←Taking false branch→
326   |  return AVERROR_INVALIDDATA;
327   |  if (codec_data_size == 0)
    43←Assuming 'codec_data_size' is not equal to 0→
    44←Taking false branch→
328   |  return 0;
329   |  
330   |  avpriv_set_pts_info(st, 64, 1, 1000);
331   |     codec_pos = avio_tell(pb);
332   |     v = avio_rb32(pb);
333   |  
334   |  if (v == MKTAG(0xfd, 'a', 'r', '.')) {
    45←Assuming the condition is true→
    46←Taking true branch→
335   |  /* ra type header */
336   |  if (rm_read_audio_stream_info(s, pb, st, rst, 0))
    47←Calling 'rm_read_audio_stream_info'→
337   |  return -1;
338   |     } else if (v == MKBETAG('L', 'S', 'D', ':')) {
339   |         avio_seek(pb, -4, SEEK_CUR);
340   |  if ((ret = rm_read_extradata(s, pb, st->codecpar, codec_data_size)) < 0)
341   |  return ret;
342   |  
343   |         st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
344   |         st->codecpar->codec_tag  = AV_RL32(st->codecpar->extradata);
345   |         st->codecpar->codec_id   = ff_codec_get_id(ff_rm_codec_tags,
346   |                                                 st->codecpar->codec_tag);
347   |     } else if(mime && !strcmp(mime, "logical-fileinfo")){
348   |  int stream_count, rule_count, property_count, i;
349   |         ff_free_stream(s, st);
350   |  if (avio_rb16(pb) != 0) {
351   |             av_log(s, AV_LOG_WARNING, "Unsupported version\n");
352   |  goto skip;
353   |         }
354   |         stream_count = avio_rb16(pb);
355   |         avio_skip(pb, 6*stream_count);
356   |         rule_count = avio_rb16(pb);
357   |         avio_skip(pb, 2*rule_count);
358   |         property_count = avio_rb16(pb);
359   |  for(i=0; i<property_count; i++){
360   |             uint8_t name[128], val[128];
361   |             avio_rb32(pb);
362   |  if (avio_rb16(pb) != 0) {
363   |                 av_log(s, AV_LOG_WARNING, "Unsupported Name value property version\n");
364   |  goto skip; //FIXME skip just this one
365   |             }
366   |             get_str8(pb, name, sizeof(name));
1129  |  break;
1130  |         }
1131  |  
1132  |         avio_skip(s->pb, len);
1133  |     }
1134  |     *ppos = pos;
1135  |  return dts;
1136  | }
1137  |  
1138  | static int rm_read_seek(AVFormatContext *s, int stream_index,
1139  |                         int64_t pts, int flags)
1140  | {
1141  |     RMDemuxContext *rm = s->priv_data;
1142  |  
1143  |  if (ff_seek_frame_binary(s, stream_index, pts, flags) < 0)
1144  |  return -1;
1145  |     rm->audio_pkt_cnt = 0;
1146  |  return 0;
1147  | }
1148  |  
1149  |  
1150  | const AVInputFormat ff_rm_demuxer = {
1151  |     .name           = "rm",
1152  |     .long_name      = NULL_IF_CONFIG_SMALL("RealMedia"),
1153  |     .priv_data_size = sizeof(RMDemuxContext),
1154  |     .read_probe     = rm_probe,
1155  |     .read_header    = rm_read_header,
1156  |     .read_packet    = rm_read_packet,
1157  |     .read_close     = rm_read_close,
1158  |     .read_timestamp = rm_read_dts,
1159  |     .read_seek      = rm_read_seek,
1160  | };
1161  |  
1162  | const AVInputFormat ff_rdt_demuxer = {
1163  |     .name           = "rdt",
1164  |     .long_name      = NULL_IF_CONFIG_SMALL("RDT demuxer"),
1165  |     .priv_data_size = sizeof(RMDemuxContext),
1166  |     .read_close     = rm_read_close,
1167  |     .flags          = AVFMT_NOFILE,
1168  | };
1169  |  
1170  | static int ivr_probe(const AVProbeData *p)
1171  | {
1172  |  if (memcmp(p->buf, ".R1M\x0\x1\x1", 7) &&
1173  |         memcmp(p->buf, ".REC", 4))
1174  |  return 0;
1175  |  
1176  |  return AVPROBE_SCORE_MAX;
1177  | }
1178  |  
1179  | static int ivr_read_header(AVFormatContext *s)
1180  | {
1181  |  unsigned tag, type, len, tlen, value;
1182  |  int i, j, n, count, nb_streams = 0, ret;
1183  |     uint8_t key[256], val[256];
1184  |     AVIOContext *pb = s->pb;
1185  |     AVStream *st;
1186  |     int64_t pos, offset=0, temp;
1187  |  
1188  |     pos = avio_tell(pb);
1189  |     tag = avio_rl32(pb);
1190  |  if (tag == MKTAG('.','R','1','M')) {
    1Assuming the condition is false→
    2←Taking false branch→
1191  |  if (avio_rb16(pb) != 1)
1192  |  return AVERROR_INVALIDDATA;
1193  |  if (avio_r8(pb) != 1)
1194  |  return AVERROR_INVALIDDATA;
1195  |         len = avio_rb32(pb);
1196  |         avio_skip(pb, len);
1197  |         avio_skip(pb, 5);
1198  |         temp = avio_rb64(pb);
1199  |  while (!avio_feof(pb) && temp) {
1200  |             offset = temp;
1201  |             temp = avio_rb64(pb);
1202  |         }
1203  |  if (offset <= 0)
1204  |  return AVERROR_INVALIDDATA;
1205  |         avio_skip(pb, offset - avio_tell(pb));
1206  |  if (avio_r8(pb) != 1)
1207  |  return AVERROR_INVALIDDATA;
1208  |         len = avio_rb32(pb);
1209  |         avio_skip(pb, len);
1210  |  if (avio_r8(pb) != 2)
1211  |  return AVERROR_INVALIDDATA;
1212  |         avio_skip(pb, 16);
1213  |         pos = avio_tell(pb);
1214  |         tag = avio_rl32(pb);
1215  |     }
1216  |  
1217  |  if (tag != MKTAG('.','R','E','C'))
    3←Assuming the condition is false→
    4←Taking false branch→
1218  |  return AVERROR_INVALIDDATA;
1219  |  
1220  |  if (avio_r8(pb) != 0)
    5←Assuming the condition is false→
    6←Taking false branch→
1221  |  return AVERROR_INVALIDDATA;
1222  |  count = avio_rb32(pb);
1223  |  for (i = 0; i < count; i++) {
    7←Assuming 'i' is < 'count'→
    8←Loop condition is true.  Entering loop body→
    18←Assuming 'i' is >= 'count'→
    19←Loop condition is false. Execution continues on line 1253→
1224  |  if (avio_feof(pb))
    9←Assuming the condition is false→
    10←Taking false branch→
1225  |  return AVERROR_INVALIDDATA;
1226  |  
1227  |  type = avio_r8(pb);
1228  |         tlen = avio_rb32(pb);
1229  |         avio_get_str(pb, tlen, key, sizeof(key));
1230  |         len = avio_rb32(pb);
1231  |  if (type == 5) {
    11←Assuming 'type' is not equal to 5→
    12←Taking false branch→
1232  |             avio_get_str(pb, len, val, sizeof(val));
1233  |             av_log(s, AV_LOG_DEBUG, "%s = '%s'\n", key, val);
1234  |         } else if (type == 4) {
    13←Assuming 'type' is not equal to 4→
1235  |             av_log(s, AV_LOG_DEBUG, "%s = '0x", key);
1236  |  for (j = 0; j < len; j++) {
1237  |  if (avio_feof(pb))
1238  |  return AVERROR_INVALIDDATA;
1239  |                 av_log(s, AV_LOG_DEBUG, "%X", avio_r8(pb));
1240  |             }
1241  |             av_log(s, AV_LOG_DEBUG, "'\n");
1242  |         } else if (len == 4 && type == 3 && !strncmp(key, "StreamCount", tlen)) {
    14←Assuming 'len' is equal to 4→
    15←Assuming 'type' is equal to 3→
    16←Assuming the condition is true→
    17←Taking true branch→
1243  |  nb_streams = value = avio_rb32(pb);
1244  |         } else if (len == 4 && type == 3) {
1245  |             value = avio_rb32(pb);
1246  |             av_log(s, AV_LOG_DEBUG, "%s = %d\n", key, value);
1247  |         } else {
1248  |             av_log(s, AV_LOG_DEBUG, "Skipping unsupported key: %s\n", key);
1249  |             avio_skip(pb, len);
1250  |         }
1251  |  }
1252  |  
1253  |  for (n = 0; n < nb_streams; n++) {
    20←Assuming 'n' is < 'nb_streams'→
1254  |  if (!(st = avformat_new_stream(s, NULL)) ||
    21←Assuming 'st' is non-null→
    23←Taking false branch→
1255  |  !(st->priv_data = ff_rm_alloc_rmstream())) {
    22←Assuming field 'priv_data' is non-null→
1256  |             ret = AVERROR(ENOMEM);
1257  |  goto fail;
1258  |         }
1259  |  
1260  |  if (avio_r8(pb) != 1)
    24←Assuming the condition is false→
    25←Taking false branch→
1261  |  goto invalid_data;
1262  |  
1263  |  count = avio_rb32(pb);
1264  |  for (i = 0; i < count; i++) {
    26←Assuming 'i' is < 'count'→
    27←Loop condition is true.  Entering loop body→
1265  |  if (avio_feof(pb))
    28←Assuming the condition is false→
    29←Taking false branch→
1266  |  goto invalid_data;
1267  |  
1268  |  type = avio_r8(pb);
1269  |             tlen  = avio_rb32(pb);
1270  |             avio_get_str(pb, tlen, key, sizeof(key));
1271  |             len  = avio_rb32(pb);
1272  |  if (type == 5) {
    30←Assuming 'type' is not equal to 5→
1273  |                 avio_get_str(pb, len, val, sizeof(val));
1274  |                 av_log(s, AV_LOG_DEBUG, "%s = '%s'\n", key, val);
1275  |             } else if (type == 4 && !strncmp(key, "OpaqueData", tlen)) {
    31←Assuming 'type' is equal to 4→
    32←Assuming the condition is true→
    33←Taking true branch→
1276  |  ret = ffio_ensure_seekback(pb, 4);
1277  |  if (ret < 0)
    34←Assuming 'ret' is >= 0→
    35←Taking false branch→
1278  |  goto fail;
1279  |  if (avio_rb32(pb) == MKBETAG('M', 'L', 'T', 'I')) {
    36←Assuming the condition is false→
    37←Taking false branch→
1280  |                     ret = rm_read_multi(s, pb, st, NULL);
1281  |                 } else {
1282  |  if (avio_feof(pb))
    38←Assuming the condition is false→
    39←Taking false branch→
1283  |  goto invalid_data;
1284  |  avio_seek(pb, -4, SEEK_CUR);
1285  |  ret = ff_rm_read_mdpr_codecdata(s, pb, st, st->priv_data, len, NULL);
    40←Calling 'ff_rm_read_mdpr_codecdata'→
1286  |                 }
1287  |  
1288  |  if (ret < 0)
1289  |  goto fail;
1290  |             } else if (type == 4) {
1291  |  int j;
1292  |  
1293  |                 av_log(s, AV_LOG_DEBUG, "%s = '0x", key);
1294  |  for (j = 0; j < len; j++) {
1295  |  if (avio_feof(pb))
1296  |  goto invalid_data;
1297  |                     av_log(s, AV_LOG_DEBUG, "%X", avio_r8(pb));
1298  |                 }
1299  |                 av_log(s, AV_LOG_DEBUG, "'\n");
1300  |             } else if (len == 4 && type == 3 && !strncmp(key, "Duration", tlen)) {
1301  |                 st->duration = avio_rb32(pb);
1302  |             } else if (len == 4 && type == 3) {
1303  |                 value = avio_rb32(pb);
1304  |                 av_log(s, AV_LOG_DEBUG, "%s = %d\n", key, value);
1305  |             } else {
1306  |                 av_log(s, AV_LOG_DEBUG, "Skipping unsupported key: %s\n", key);
1307  |                 avio_skip(pb, len);
1308  |             }
1309  |         }
1310  |     }
1311  |  
1312  |  if (avio_r8(pb) != 6)
1313  |  goto invalid_data;
1314  |     avio_skip(pb, 12);
1315  |     avio_seek(pb, avio_rb64(pb) + pos, SEEK_SET);