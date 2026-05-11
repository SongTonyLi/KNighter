### Report Summary

File:| format/ty.c  
---|---  
Warning:| line 182, column 16  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


119   |  int num_recs)
120   | {
121   |     TyRecHdr *hdrs, *rec_hdr;
122   |  int i;
123   |  
124   |     hdrs = av_calloc(num_recs, sizeof(TyRecHdr));
125   |  if (!hdrs)
126   |  return NULL;
127   |  
128   |  for (i = 0; i < num_recs; i++) {
129   |  const uint8_t *record_header = buf + (i * 16);
130   |  
131   |         rec_hdr = &hdrs[i];     /* for brevity */
132   |         rec_hdr->rec_type = record_header[3];
133   |         rec_hdr->subrec_type = record_header[2] & 0x0f;
134   |  if ((record_header[0] & 0x80) == 0x80) {
135   |             uint8_t b1, b2;
136   |  
137   |  /* marker bit 2 set, so read extended data */
138   |             b1 = (((record_header[0] & 0x0f) << 4) |
139   |                   ((record_header[1] & 0xf0) >> 4));
140   |             b2 = (((record_header[1] & 0x0f) << 4) |
141   |                   ((record_header[2] & 0xf0) >> 4));
142   |  
143   |             rec_hdr->ex[0] = b1;
144   |             rec_hdr->ex[1] = b2;
145   |             rec_hdr->rec_size = 0;
146   |             rec_hdr->ty_pts = 0;
147   |         } else {
148   |             rec_hdr->rec_size = (record_header[0] << 8 |
149   |                                  record_header[1]) << 4 |
150   |                                 (record_header[2] >> 4);
151   |             rec_hdr->ty_pts = AV_RB64(&record_header[8]);
152   |         }
153   |     }
154   |  return hdrs;
155   | }
156   |  
157   | static int find_es_header(const uint8_t *header,
158   |  const uint8_t *buffer, int search_len)
159   | {
160   |  int count;
161   |  
162   |  for (count = 0; count < search_len; count++) {
163   |  if (!memcmp(&buffer[count], header, 4))
164   |  return count;
165   |     }
166   |  return -1;
167   | }
168   |  
169   | static int analyze_chunk(AVFormatContext *s, const uint8_t *chunk)
170   | {
171   |  TYDemuxContext *ty = s->priv_data;
172   |  int num_recs, i;
173   |     TyRecHdr *hdrs;
174   |  int num_6e0, num_be0, num_9c0, num_3c0;
175   |  
176   |  /* skip if it's a Part header */
177   |  if (AV_RB32(&chunk[0]) == TIVO_PES_FILEID)
    3←Assuming the condition is false→
    4←Taking false branch→
178   |  return 0;
179   |  
180   |  /* number of records in chunk (we ignore high order byte;
181   |  * rarely are there > 256 chunks & we don't need that many anyway) */
182   |  num_recs = chunk[0];
    5←buffer read by avio_read may be partially uninitialized
183   |  if (num_recs < 5) {
184   |  /* try again with the next chunk.  Sometimes there are dead ones */
185   |  return 0;
186   |     }
187   |  
188   |     chunk += 4;       /* skip past rec count & SEQ bytes */
189   |  ff_dlog(s, "probe: chunk has %d recs\n", num_recs);
190   |     hdrs = parse_chunk_headers(chunk, num_recs);
191   |  if (!hdrs)
192   |  return AVERROR(ENOMEM);
193   |  
194   |  /* scan headers.
195   |  * 1. check video packets.  Presence of 0x6e0 means S1.
196   |  *    No 6e0 but have be0 means S2.
197   |  * 2. probe for audio 0x9c0 vs 0x3c0 (AC3 vs Mpeg)
198   |  *    If AC-3, then we have DTivo.
199   |  *    If MPEG, search for PTS offset.  This will determine SA vs. DTivo.
200   |  */
201   |     num_6e0 = num_be0 = num_9c0 = num_3c0 = 0;
202   |  for (i = 0; i < num_recs; i++) {
203   |  switch (hdrs[i].subrec_type << 8 | hdrs[i].rec_type) {
204   |  case 0x6e0:
205   |             num_6e0++;
206   |  break;
207   |  case 0xbe0:
208   |             num_be0++;
209   |  break;
210   |  case 0x3c0:
211   |             num_3c0++;
212   |  break;
230   |     }
231   |  if (num_9c0 > 0) {
232   |  ff_dlog(s, "detected AC-3 Audio (DTivo)\n");
233   |         ty->audio_type = TIVO_AUDIO_AC3;
234   |         ty->tivo_type = TIVO_TYPE_DTIVO;
235   |         ty->pts_offset = AC3_PTS_OFFSET;
236   |         ty->pes_length = AC3_PES_LENGTH;
237   |     } else if (num_3c0 > 0) {
238   |         ty->audio_type = TIVO_AUDIO_MPEG;
239   |  ff_dlog(s, "detected MPEG Audio\n");
240   |     }
241   |  
242   |  /* if tivo_type still unknown, we can check PTS location
243   |  * in MPEG packets to determine tivo_type */
244   |  if (ty->tivo_type == TIVO_TYPE_UNKNOWN) {
245   |         uint32_t data_offset = 16 * num_recs;
246   |  
247   |  for (i = 0; i < num_recs; i++) {
248   |  if (data_offset + hdrs[i].rec_size > CHUNK_SIZE)
249   |  break;
250   |  
251   |  if ((hdrs[i].subrec_type << 8 | hdrs[i].rec_type) == 0x3c0 && hdrs[i].rec_size > 15) {
252   |  /* first make sure we're aligned */
253   |  int pes_offset = find_es_header(ty_MPEGAudioPacket,
254   |                         &chunk[data_offset], 5);
255   |  if (pes_offset >= 0) {
256   |  /* pes found. on SA, PES has hdr data at offset 6, not PTS. */
257   |  if ((chunk[data_offset + 6 + pes_offset] & 0x80) == 0x80) {
258   |  /* S1SA or S2(any) Mpeg Audio (PES hdr, not a PTS start) */
259   |  if (ty->tivo_series == TIVO_SERIES1)
260   |  ff_dlog(s, "detected Stand-Alone Tivo\n");
261   |                         ty->tivo_type = TIVO_TYPE_SA;
262   |                         ty->pts_offset = SA_PTS_OFFSET;
263   |                     } else {
264   |  if (ty->tivo_series == TIVO_SERIES1)
265   |  ff_dlog(s, "detected DirecTV Tivo\n");
266   |                         ty->tivo_type = TIVO_TYPE_DTIVO;
267   |                         ty->pts_offset = DTIVO_PTS_OFFSET;
268   |                     }
269   |  break;
270   |                 }
271   |             }
272   |             data_offset += hdrs[i].rec_size;
273   |         }
274   |     }
275   |     av_free(hdrs);
276   |  
277   |  return 0;
278   | }
279   |  
280   | static int ty_read_header(AVFormatContext *s)
281   | {
282   |  TYDemuxContext *ty = s->priv_data;
283   |     AVIOContext *pb = s->pb;
284   |     AVStream *st, *ast;
285   |  int i, ret = 0;
286   |  
287   |     ty->first_audio_pts = AV_NOPTS_VALUE;
288   |     ty->last_audio_pts = AV_NOPTS_VALUE;
289   |     ty->last_video_pts = AV_NOPTS_VALUE;
290   |  
291   |  for (i = 0; i < CHUNK_PEEK_COUNT; i++) {
    1Loop condition is true.  Entering loop body→
292   |  avio_read(pb, ty->chunk, CHUNK_SIZE);
293   |  
294   |  ret = analyze_chunk(s, ty->chunk);
    2←Calling 'analyze_chunk'→
295   |  if (ret < 0)
296   |  return ret;
297   |  if (ty->tivo_series != TIVO_SERIES_UNKNOWN &&
298   |             ty->audio_type  != TIVO_AUDIO_UNKNOWN &&
299   |             ty->tivo_type   != TIVO_TYPE_UNKNOWN)
300   |  break;
301   |     }
302   |  
303   |  if (ty->tivo_series == TIVO_SERIES_UNKNOWN ||
304   |         ty->audio_type == TIVO_AUDIO_UNKNOWN ||
305   |         ty->tivo_type == TIVO_TYPE_UNKNOWN)
306   |  return AVERROR_INVALIDDATA;
307   |  
308   |     st = avformat_new_stream(s, NULL);
309   |  if (!st)
310   |  return AVERROR(ENOMEM);
311   |     st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
312   |     st->codecpar->codec_id   = AV_CODEC_ID_MPEG2VIDEO;
313   |     ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL_RAW;
314   |     avpriv_set_pts_info(st, 64, 1, 90000);
315   |  
316   |     ast = avformat_new_stream(s, NULL);
317   |  if (!ast)
318   |  return AVERROR(ENOMEM);
319   |     ast->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
320   |  
321   |  if (ty->audio_type == TIVO_AUDIO_MPEG) {
322   |         ast->codecpar->codec_id = AV_CODEC_ID_MP2;
323   |         ffstream(ast)->need_parsing = AVSTREAM_PARSE_FULL_RAW;
324   |     } else {