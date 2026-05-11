### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/mpeg.c  
---|---  
Warning:| line 299, column 51  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


178   |         state = ((state << 8) | v) & 0xffffff;
179   |     }
180   |     val = -1;
181   |  
182   | found:
183   |     *header_state = state;
184   |     *size_ptr     = n;
185   |  return val;
186   | }
187   |  
188   | /**
189   |  * Extract stream types from a program stream map
190   |  * According to ISO/IEC 13818-1 ('MPEG-2 Systems') table 2-35
191   |  *
192   |  * @return number of bytes occupied by PSM in the bitstream
193   |  */
194   | static long mpegps_psm_parse(MpegDemuxContext *m, AVIOContext *pb)
195   | {
196   |  int psm_length, ps_info_length, es_map_length;
197   |  
198   |     psm_length = avio_rb16(pb);
199   |     avio_r8(pb);
200   |     avio_r8(pb);
201   |     ps_info_length = avio_rb16(pb);
202   |  
203   |  /* skip program_stream_info */
204   |     avio_skip(pb, ps_info_length);
205   |  /*es_map_length = */avio_rb16(pb);
206   |  /* Ignore es_map_length, trust psm_length */
207   |     es_map_length = psm_length - ps_info_length - 10;
208   |  
209   |  /* at least one es available? */
210   |  while (es_map_length >= 4) {
211   |  unsigned char type      = avio_r8(pb);
212   |  unsigned char es_id     = avio_r8(pb);
213   |         uint16_t es_info_length = avio_rb16(pb);
214   |  
215   |  /* remember mapping from stream id to stream type */
216   |         m->psm_es_type[es_id] = type;
217   |  /* skip program_stream_info */
218   |         avio_skip(pb, es_info_length);
219   |         es_map_length -= 4 + es_info_length;
220   |     }
221   |     avio_rb32(pb); /* crc32 */
222   |  return 2 + psm_length;
223   | }
224   |  
225   | /* read the next PES header. Return its position in ppos
226   |  * (if not NULL), and its start code, pts and dts.
227   |  */
228   | static int mpegps_read_pes_header(AVFormatContext *s,
229   |                                   int64_t *ppos, int *pstart_code,
230   |                                   int64_t *ppts, int64_t *pdts)
231   | {
232   |  MpegDemuxContext *m = s->priv_data;
233   |  int len, size, startcode, c, flags, header_len;
234   |  int pes_ext, ext2_len, id_ext, skip;
235   |     int64_t pts, dts;
236   |  int64_t last_sync = avio_tell(s->pb);
237   |  
238   | error_redo:
239   |  avio_seek(s->pb, last_sync, SEEK_SET);
240   | redo:
241   |  /* next start code (should be immediately after) */
242   |  m->header_state = 0xff;
243   |  size      = MAX_SYNC_SIZE;
244   |     startcode = find_next_start_code(s->pb, &size, &m->header_state);
245   |     last_sync = avio_tell(s->pb);
246   |  if (startcode < 0) {
    1Assuming 'startcode' is >= 0→
    2←Taking false branch→
247   |  if (avio_feof(s->pb))
248   |  return AVERROR_EOF;
249   |  // FIXME we should remember header_state
250   |  return FFERROR_REDO;
251   |     }
252   |  
253   |  if (startcode == PACK_START_CODE)
    3←Assuming 'startcode' is not equal to PACK_START_CODE→
    4←Taking false branch→
254   |  goto redo;
255   |  if (startcode == SYSTEM_HEADER_START_CODE)
    5←Assuming 'startcode' is not equal to SYSTEM_HEADER_START_CODE→
    6←Taking false branch→
256   |  goto redo;
257   |  if (startcode == PADDING_STREAM) {
    7←Assuming 'startcode' is not equal to PADDING_STREAM→
    8←Taking false branch→
258   |         avio_skip(s->pb, avio_rb16(s->pb));
259   |  goto redo;
260   |     }
261   |  if (startcode == PRIVATE_STREAM_2) {
    9←Assuming 'startcode' is equal to PRIVATE_STREAM_2→
    10←Taking true branch→
262   |  if (!m->sofdec) {
    11←Assuming field 'sofdec' is 0→
    12←Taking true branch→
263   |  /* Need to detect whether this from a DVD or a 'Sofdec' stream */
264   |  int len = avio_rb16(s->pb);
265   |  int bytesread = 0;
266   |             uint8_t *ps2buf = av_malloc(len);
267   |  
268   |  if (ps2buf) {
    13←Assuming 'ps2buf' is non-null→
    14←Taking true branch→
269   |  bytesread = avio_read(s->pb, ps2buf, len);
270   |  
271   |  if (bytesread != len) {
    15←Assuming 'bytesread' is equal to 'len'→
    16←Taking false branch→
272   |                     avio_skip(s->pb, len - bytesread);
273   |                 } else {
274   |  uint8_t *p = 0;
275   |  if (len >= 6)
    17←Assuming 'len' is >= 6→
    18←Taking true branch→
276   |  p = memchr(ps2buf, 'S', len - 5);
277   |  
278   |  if (p)
    19←Assuming 'p' is null→
    20←Taking false branch→
279   |                         m->sofdec = !memcmp(p+1, "ofdec", 5);
280   |  
281   |  m->sofdec -= !m->sofdec;
282   |  
283   |  if (m->sofdec20.1Field 'sofdec' is < 0 < 0) {
284   |  if (len == 980  && ps2buf[0] == 0) {
    21←Assuming 'len' is not equal to 980→
285   |  /* PCI structure? */
286   |                             uint32_t startpts = AV_RB32(ps2buf + 0x0d);
287   |                             uint32_t endpts = AV_RB32(ps2buf + 0x11);
288   |                             uint8_t hours = ((ps2buf[0x19] >> 4) * 10) + (ps2buf[0x19] & 0x0f);
289   |                             uint8_t mins  = ((ps2buf[0x1a] >> 4) * 10) + (ps2buf[0x1a] & 0x0f);
290   |                             uint8_t secs  = ((ps2buf[0x1b] >> 4) * 10) + (ps2buf[0x1b] & 0x0f);
291   |  
292   |                             m->dvd = (hours <= 23 &&
293   |                                       mins  <= 59 &&
294   |                                       secs  <= 59 &&
295   |                                       (ps2buf[0x19] & 0x0f) < 10 &&
296   |                                       (ps2buf[0x1a] & 0x0f) < 10 &&
297   |                                       (ps2buf[0x1b] & 0x0f) < 10 &&
298   |                                       endpts >= startpts);
299   |                         } else if (len == 1018 && ps2buf[0] == 1) {
    22←Assuming 'len' is equal to 1018→
    23←buffer read by avio_read may be partially uninitialized
300   |  /* DSI structure? */
301   |                             uint8_t hours = ((ps2buf[0x1d] >> 4) * 10) + (ps2buf[0x1d] & 0x0f);
302   |                             uint8_t mins  = ((ps2buf[0x1e] >> 4) * 10) + (ps2buf[0x1e] & 0x0f);
303   |                             uint8_t secs  = ((ps2buf[0x1f] >> 4) * 10) + (ps2buf[0x1f] & 0x0f);
304   |  
305   |                             m->dvd = (hours <= 23 &&
306   |                                       mins  <= 59 &&
307   |                                       secs  <= 59 &&
308   |                                       (ps2buf[0x1d] & 0x0f) < 10 &&
309   |                                       (ps2buf[0x1e] & 0x0f) < 10 &&
310   |                                       (ps2buf[0x1f] & 0x0f) < 10);
311   |                         }
312   |                     }
313   |                 }
314   |  
315   |                 av_free(ps2buf);
316   |  
317   |  /* If this isn't a DVD packet or no memory
318   |  * could be allocated, just ignore it.
319   |  * If we did, move back to the start of the
320   |  * packet (plus 'length' field) */
321   |  if (!m->dvd || avio_skip(s->pb, -(len + 2)) < 0) {
322   |  /* Skip back failed.
323   |  * This packet will be lost but that can't be helped
324   |  * if we can't skip back
325   |  */
326   |  goto redo;
327   |                 }
328   |             } else {
329   |  /* No memory */