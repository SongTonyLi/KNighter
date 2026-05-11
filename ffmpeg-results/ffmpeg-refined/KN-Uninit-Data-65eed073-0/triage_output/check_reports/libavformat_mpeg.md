### Report Summary

File:| format/mpeg.c  
---|---  
Warning:| line 307, column 51  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


186   |         state = ((state << 8) | v) & 0xffffff;
187   |     }
188   |     val = -1;
189   |  
190   | found:
191   |     *header_state = state;
192   |     *size_ptr     = n;
193   |  return val;
194   | }
195   |  
196   | /**
197   |  * Extract stream types from a program stream map
198   |  * According to ISO/IEC 13818-1 ('MPEG-2 Systems') table 2-35
199   |  *
200   |  * @return number of bytes occupied by PSM in the bitstream
201   |  */
202   | static long mpegps_psm_parse(MpegDemuxContext *m, AVIOContext *pb)
203   | {
204   |  int psm_length, ps_info_length, es_map_length;
205   |  
206   |     psm_length = avio_rb16(pb);
207   |     avio_r8(pb);
208   |     avio_r8(pb);
209   |     ps_info_length = avio_rb16(pb);
210   |  
211   |  /* skip program_stream_info */
212   |     avio_skip(pb, ps_info_length);
213   |  /*es_map_length = */avio_rb16(pb);
214   |  /* Ignore es_map_length, trust psm_length */
215   |     es_map_length = psm_length - ps_info_length - 10;
216   |  
217   |  /* at least one es available? */
218   |  while (es_map_length >= 4) {
219   |  unsigned char type      = avio_r8(pb);
220   |  unsigned char es_id     = avio_r8(pb);
221   |         uint16_t es_info_length = avio_rb16(pb);
222   |  
223   |  /* remember mapping from stream id to stream type */
224   |         m->psm_es_type[es_id] = type;
225   |  /* skip program_stream_info */
226   |         avio_skip(pb, es_info_length);
227   |         es_map_length -= 4 + es_info_length;
228   |     }
229   |     avio_rb32(pb); /* crc32 */
230   |  return 2 + psm_length;
231   | }
232   |  
233   | /* read the next PES header. Return its position in ppos
234   |  * (if not NULL), and its start code, pts and dts.
235   |  */
236   | static int mpegps_read_pes_header(AVFormatContext *s,
237   |                                   int64_t *ppos, int *pstart_code,
238   |                                   int64_t *ppts, int64_t *pdts)
239   | {
240   |  MpegDemuxContext *m = s->priv_data;
241   |  int len, size, startcode, c, flags, header_len;
242   |  int pes_ext, ext2_len, id_ext, skip;
243   |     int64_t pts, dts;
244   |  int64_t last_sync = avio_tell(s->pb);
245   |  
246   | error_redo:
247   |  avio_seek(s->pb, last_sync, SEEK_SET);
248   | redo:
249   |  /* next start code (should be immediately after) */
250   |  m->header_state = 0xff;
251   |  size      = MAX_SYNC_SIZE;
252   |     startcode = find_next_start_code(s->pb, &size, &m->header_state);
253   |     last_sync = avio_tell(s->pb);
254   |  if (startcode < 0) {
    1Assuming 'startcode' is >= 0→
    2←Taking false branch→
255   |  if (avio_feof(s->pb))
256   |  return AVERROR_EOF;
257   |  // FIXME we should remember header_state
258   |  return FFERROR_REDO;
259   |     }
260   |  
261   |  if (startcode == PACK_START_CODE)
    3←Assuming 'startcode' is not equal to PACK_START_CODE→
    4←Taking false branch→
262   |  goto redo;
263   |  if (startcode == SYSTEM_HEADER_START_CODE)
    5←Assuming 'startcode' is not equal to SYSTEM_HEADER_START_CODE→
    6←Taking false branch→
264   |  goto redo;
265   |  if (startcode == PADDING_STREAM) {
    7←Assuming 'startcode' is not equal to PADDING_STREAM→
    8←Taking false branch→
266   |         avio_skip(s->pb, avio_rb16(s->pb));
267   |  goto redo;
268   |     }
269   |  if (startcode == PRIVATE_STREAM_2) {
    9←Assuming 'startcode' is equal to PRIVATE_STREAM_2→
    10←Taking true branch→
270   |  if (!m->sofdec) {
    11←Assuming field 'sofdec' is 0→
    12←Taking true branch→
271   |  /* Need to detect whether this from a DVD or a 'Sofdec' stream */
272   |  int len = avio_rb16(s->pb);
273   |  int bytesread = 0;
274   |             uint8_t *ps2buf = av_malloc(len);
275   |  
276   |  if (ps2buf) {
    13←Assuming 'ps2buf' is non-null→
    14←Taking true branch→
277   |  bytesread = avio_read(s->pb, ps2buf, len);
278   |  
279   |  if (bytesread != len) {
    15←Assuming 'bytesread' is equal to 'len'→
    16←Taking false branch→
280   |                     avio_skip(s->pb, len - bytesread);
281   |                 } else {
282   |  uint8_t *p = 0;
283   |  if (len >= 6)
    17←Assuming 'len' is >= 6→
    18←Taking true branch→
284   |  p = memchr(ps2buf, 'S', len - 5);
285   |  
286   |  if (p)
    19←Assuming 'p' is null→
    20←Taking false branch→
287   |                         m->sofdec = !memcmp(p+1, "ofdec", 5);
288   |  
289   |  m->sofdec -= !m->sofdec;
290   |  
291   |  if (m->sofdec20.1Field 'sofdec' is < 0 < 0) {
292   |  if (len == 980  && ps2buf[0] == 0) {
    21←Assuming 'len' is not equal to 980→
293   |  /* PCI structure? */
294   |                             uint32_t startpts = AV_RB32(ps2buf + 0x0d);
295   |                             uint32_t endpts = AV_RB32(ps2buf + 0x11);
296   |                             uint8_t hours = ((ps2buf[0x19] >> 4) * 10) + (ps2buf[0x19] & 0x0f);
297   |                             uint8_t mins  = ((ps2buf[0x1a] >> 4) * 10) + (ps2buf[0x1a] & 0x0f);
298   |                             uint8_t secs  = ((ps2buf[0x1b] >> 4) * 10) + (ps2buf[0x1b] & 0x0f);
299   |  
300   |                             m->dvd = (hours <= 23 &&
301   |                                       mins  <= 59 &&
302   |                                       secs  <= 59 &&
303   |                                       (ps2buf[0x19] & 0x0f) < 10 &&
304   |                                       (ps2buf[0x1a] & 0x0f) < 10 &&
305   |                                       (ps2buf[0x1b] & 0x0f) < 10 &&
306   |                                       endpts >= startpts);
307   |                         } else if (len == 1018 && ps2buf[0] == 1) {
    22←Assuming 'len' is equal to 1018→
    23←buffer read by avio_read may be partially uninitialized
308   |  /* DSI structure? */
309   |                             uint8_t hours = ((ps2buf[0x1d] >> 4) * 10) + (ps2buf[0x1d] & 0x0f);
310   |                             uint8_t mins  = ((ps2buf[0x1e] >> 4) * 10) + (ps2buf[0x1e] & 0x0f);
311   |                             uint8_t secs  = ((ps2buf[0x1f] >> 4) * 10) + (ps2buf[0x1f] & 0x0f);
312   |  
313   |                             m->dvd = (hours <= 23 &&
314   |                                       mins  <= 59 &&
315   |                                       secs  <= 59 &&
316   |                                       (ps2buf[0x1d] & 0x0f) < 10 &&
317   |                                       (ps2buf[0x1e] & 0x0f) < 10 &&
318   |                                       (ps2buf[0x1f] & 0x0f) < 10);
319   |                         }
320   |                     }
321   |                 }
322   |  
323   |                 av_free(ps2buf);
324   |  
325   |  /* If this isn't a DVD packet or no memory
326   |  * could be allocated, just ignore it.
327   |  * If we did, move back to the start of the
328   |  * packet (plus 'length' field) */
329   |  if (!m->dvd || avio_skip(s->pb, -(len + 2)) < 0) {
330   |  /* Skip back failed.
331   |  * This packet will be lost but that can't be helped
332   |  * if we can't skip back
333   |  */
334   |  goto redo;
335   |                 }
336   |             } else {
337   |  /* No memory */