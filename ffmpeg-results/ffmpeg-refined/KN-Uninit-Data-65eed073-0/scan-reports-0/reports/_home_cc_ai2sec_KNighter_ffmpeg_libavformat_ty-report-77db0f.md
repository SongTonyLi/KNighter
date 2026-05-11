### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/ty.c  
---|---  
Warning:| line 175, column 9  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


117   |  int num_recs)
118   | {
119   |     TyRecHdr *hdrs, *rec_hdr;
120   |  int i;
121   |  
122   |     hdrs = av_calloc(num_recs, sizeof(TyRecHdr));
123   |  if (!hdrs)
124   |  return NULL;
125   |  
126   |  for (i = 0; i < num_recs; i++) {
127   |  const uint8_t *record_header = buf + (i * 16);
128   |  
129   |         rec_hdr = &hdrs[i];     /* for brevity */
130   |         rec_hdr->rec_type = record_header[3];
131   |         rec_hdr->subrec_type = record_header[2] & 0x0f;
132   |  if ((record_header[0] & 0x80) == 0x80) {
133   |             uint8_t b1, b2;
134   |  
135   |  /* marker bit 2 set, so read extended data */
136   |             b1 = (((record_header[0] & 0x0f) << 4) |
137   |                   ((record_header[1] & 0xf0) >> 4));
138   |             b2 = (((record_header[1] & 0x0f) << 4) |
139   |                   ((record_header[2] & 0xf0) >> 4));
140   |  
141   |             rec_hdr->ex[0] = b1;
142   |             rec_hdr->ex[1] = b2;
143   |             rec_hdr->rec_size = 0;
144   |             rec_hdr->ty_pts = 0;
145   |         } else {
146   |             rec_hdr->rec_size = (record_header[0] << 8 |
147   |                                  record_header[1]) << 4 |
148   |                                 (record_header[2] >> 4);
149   |             rec_hdr->ty_pts = AV_RB64(&record_header[8]);
150   |         }
151   |     }
152   |  return hdrs;
153   | }
154   |  
155   | static int find_es_header(const uint8_t *header,
156   |  const uint8_t *buffer, int search_len)
157   | {
158   |  int count;
159   |  
160   |  for (count = 0; count < search_len; count++) {
161   |  if (!memcmp(&buffer[count], header, 4))
162   |  return count;
163   |     }
164   |  return -1;
165   | }
166   |  
167   | static int analyze_chunk(AVFormatContext *s, const uint8_t *chunk)
168   | {
169   |  TYDemuxContext *ty = s->priv_data;
170   |  int num_recs, i;
171   |     TyRecHdr *hdrs;
172   |  int num_6e0, num_be0, num_9c0, num_3c0;
173   |  
174   |  /* skip if it's a Part header */
175   |  if (AV_RB32(&chunk[0]) == TIVO_PES_FILEID)
    3←buffer read by avio_read may be partially uninitialized
176   |  return 0;
177   |  
178   |  /* number of records in chunk (we ignore high order byte;
179   |  * rarely are there > 256 chunks & we don't need that many anyway) */
180   |     num_recs = chunk[0];
181   |  if (num_recs < 5) {
182   |  /* try again with the next chunk.  Sometimes there are dead ones */
183   |  return 0;
184   |     }
185   |  
186   |     chunk += 4;       /* skip past rec count & SEQ bytes */
187   |  ff_dlog(s, "probe: chunk has %d recs\n", num_recs);
188   |     hdrs = parse_chunk_headers(chunk, num_recs);
189   |  if (!hdrs)
190   |  return AVERROR(ENOMEM);
191   |  
192   |  /* scan headers.
193   |  * 1. check video packets.  Presence of 0x6e0 means S1.
194   |  *    No 6e0 but have be0 means S2.
195   |  * 2. probe for audio 0x9c0 vs 0x3c0 (AC3 vs Mpeg)
196   |  *    If AC-3, then we have DTivo.
197   |  *    If MPEG, search for PTS offset.  This will determine SA vs. DTivo.
198   |  */
199   |     num_6e0 = num_be0 = num_9c0 = num_3c0 = 0;
200   |  for (i = 0; i < num_recs; i++) {
201   |  switch (hdrs[i].subrec_type << 8 | hdrs[i].rec_type) {
202   |  case 0x6e0:
203   |             num_6e0++;
204   |  break;
205   |  case 0xbe0:
228   |     }
229   |  if (num_9c0 > 0) {
230   |  ff_dlog(s, "detected AC-3 Audio (DTivo)\n");
231   |         ty->audio_type = TIVO_AUDIO_AC3;
232   |         ty->tivo_type = TIVO_TYPE_DTIVO;
233   |         ty->pts_offset = AC3_PTS_OFFSET;
234   |         ty->pes_length = AC3_PES_LENGTH;
235   |     } else if (num_3c0 > 0) {
236   |         ty->audio_type = TIVO_AUDIO_MPEG;
237   |  ff_dlog(s, "detected MPEG Audio\n");
238   |     }
239   |  
240   |  /* if tivo_type still unknown, we can check PTS location
241   |  * in MPEG packets to determine tivo_type */
242   |  if (ty->tivo_type == TIVO_TYPE_UNKNOWN) {
243   |         uint32_t data_offset = 16 * num_recs;
244   |  
245   |  for (i = 0; i < num_recs; i++) {
246   |  if (data_offset + hdrs[i].rec_size > CHUNK_SIZE)
247   |  break;
248   |  
249   |  if ((hdrs[i].subrec_type << 8 | hdrs[i].rec_type) == 0x3c0 && hdrs[i].rec_size > 15) {
250   |  /* first make sure we're aligned */
251   |  int pes_offset = find_es_header(ty_MPEGAudioPacket,
252   |                         &chunk[data_offset], 5);
253   |  if (pes_offset >= 0) {
254   |  /* pes found. on SA, PES has hdr data at offset 6, not PTS. */
255   |  if ((chunk[data_offset + 6 + pes_offset] & 0x80) == 0x80) {
256   |  /* S1SA or S2(any) Mpeg Audio (PES hdr, not a PTS start) */
257   |  if (ty->tivo_series == TIVO_SERIES1)
258   |  ff_dlog(s, "detected Stand-Alone Tivo\n");
259   |                         ty->tivo_type = TIVO_TYPE_SA;
260   |                         ty->pts_offset = SA_PTS_OFFSET;
261   |                     } else {
262   |  if (ty->tivo_series == TIVO_SERIES1)
263   |  ff_dlog(s, "detected DirecTV Tivo\n");
264   |                         ty->tivo_type = TIVO_TYPE_DTIVO;
265   |                         ty->pts_offset = DTIVO_PTS_OFFSET;
266   |                     }
267   |  break;
268   |                 }
269   |             }
270   |             data_offset += hdrs[i].rec_size;
271   |         }
272   |     }
273   |     av_free(hdrs);
274   |  
275   |  return 0;
276   | }
277   |  
278   | static int ty_read_header(AVFormatContext *s)
279   | {
280   |  TYDemuxContext *ty = s->priv_data;
281   |     AVIOContext *pb = s->pb;
282   |     AVStream *st, *ast;
283   |  int i, ret = 0;
284   |  
285   |     ty->first_audio_pts = AV_NOPTS_VALUE;
286   |     ty->last_audio_pts = AV_NOPTS_VALUE;
287   |     ty->last_video_pts = AV_NOPTS_VALUE;
288   |  
289   |  for (i = 0; i < CHUNK_PEEK_COUNT; i++) {
    1Loop condition is true.  Entering loop body→
290   |  avio_read(pb, ty->chunk, CHUNK_SIZE);
291   |  
292   |  ret = analyze_chunk(s, ty->chunk);
    2←Calling 'analyze_chunk'→
293   |  if (ret < 0)
294   |  return ret;
295   |  if (ty->tivo_series != TIVO_SERIES_UNKNOWN &&
296   |             ty->audio_type  != TIVO_AUDIO_UNKNOWN &&
297   |             ty->tivo_type   != TIVO_TYPE_UNKNOWN)
298   |  break;
299   |     }
300   |  
301   |  if (ty->tivo_series == TIVO_SERIES_UNKNOWN ||
302   |         ty->audio_type == TIVO_AUDIO_UNKNOWN ||
303   |         ty->tivo_type == TIVO_TYPE_UNKNOWN)
304   |  return AVERROR(EIO);
305   |  
306   |     st = avformat_new_stream(s, NULL);
307   |  if (!st)
308   |  return AVERROR(ENOMEM);
309   |     st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
310   |     st->codecpar->codec_id   = AV_CODEC_ID_MPEG2VIDEO;
311   |     st->internal->need_parsing         = AVSTREAM_PARSE_FULL_RAW;
312   |     avpriv_set_pts_info(st, 64, 1, 90000);
313   |  
314   |     ast = avformat_new_stream(s, NULL);
315   |  if (!ast)
316   |  return AVERROR(ENOMEM);
317   |     ast->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
318   |  
319   |  if (ty->audio_type == TIVO_AUDIO_MPEG) {
320   |         ast->codecpar->codec_id = AV_CODEC_ID_MP2;
321   |         ast->internal->need_parsing       = AVSTREAM_PARSE_FULL_RAW;
322   |     } else {