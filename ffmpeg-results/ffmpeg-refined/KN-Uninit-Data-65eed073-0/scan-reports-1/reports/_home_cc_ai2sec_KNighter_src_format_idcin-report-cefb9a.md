### Report Summary

File:| format/idcin.c  
---|---  
Warning:| line 289, column 21  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


200   |     st->start_time = 0;
201   |     idcin->video_stream_index = st->index;
202   |     st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
203   |     st->codecpar->codec_id = AV_CODEC_ID_IDCIN;
204   |     st->codecpar->codec_tag = 0;  /* no fourcc */
205   |     st->codecpar->width = width;
206   |     st->codecpar->height = height;
207   |  
208   |  /* load up the Huffman tables into extradata */
209   |  if ((ret = ff_get_extradata(s, st->codecpar, pb, HUFFMAN_TABLE_SIZE)) < 0)
210   |  return ret;
211   |  
212   |  if (idcin->audio_present) {
213   |         idcin->audio_present = 1;
214   |         st = avformat_new_stream(s, NULL);
215   |  if (!st)
216   |  return AVERROR(ENOMEM);
217   |         avpriv_set_pts_info(st, 63, 1, sample_rate);
218   |         st->start_time = 0;
219   |         idcin->audio_stream_index = st->index;
220   |         st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
221   |         st->codecpar->codec_tag = 1;
222   |         av_channel_layout_default(&st->codecpar->ch_layout, channels);
223   |         st->codecpar->sample_rate = sample_rate;
224   |         st->codecpar->bits_per_coded_sample = bytes_per_sample * 8;
225   |         st->codecpar->bit_rate = sample_rate * bytes_per_sample * 8 * channels;
226   |         st->codecpar->block_align = idcin->block_align = bytes_per_sample * channels;
227   |  if (bytes_per_sample == 1)
228   |             st->codecpar->codec_id = AV_CODEC_ID_PCM_U8;
229   |  else
230   |             st->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE;
231   |  
232   |  if (sample_rate % 14 != 0) {
233   |             idcin->audio_chunk_size1 = (sample_rate / 14) *
234   |             bytes_per_sample * channels;
235   |             idcin->audio_chunk_size2 = (sample_rate / 14 + 1) *
236   |                 bytes_per_sample * channels;
237   |         } else {
238   |             idcin->audio_chunk_size1 = idcin->audio_chunk_size2 =
239   |                 (sample_rate / 14) * bytes_per_sample * channels;
240   |         }
241   |         idcin->current_audio_chunk = 0;
242   |     }
243   |  
244   |     idcin->next_chunk_is_video = 1;
245   |     idcin->first_pkt_pos = avio_tell(s->pb);
246   |  
247   |  return 0;
248   | }
249   |  
250   | static int idcin_read_packet(AVFormatContext *s,
251   |                              AVPacket *pkt)
252   | {
253   |  int ret;
254   |  unsigned int command;
255   |  unsigned int chunk_size;
256   |     IdcinDemuxContext *idcin = s->priv_data;
257   |     AVIOContext *pb = s->pb;
258   |  int i;
259   |  int palette_scale;
260   |  unsigned char r, g, b;
261   |  unsigned char palette_buffer[768];
262   |     uint32_t palette[256];
263   |  
264   |  if (avio_feof(s->pb))
    1Assuming the condition is false→
    2←Taking false branch→
265   |  return s->pb->error ? s->pb->error : AVERROR_EOF;
266   |  
267   |  if (idcin->next_chunk_is_video) {
    3←Assuming field 'next_chunk_is_video' is not equal to 0→
    4←Taking true branch→
268   |  command = avio_rl32(pb);
269   |  if (command == 2) {
    5←Assuming 'command' is not equal to 2→
    6←Taking false branch→
270   |  return AVERROR_INVALIDDATA;
271   |         } else if (command == 1) {
    7←Assuming 'command' is equal to 1→
    8←Taking true branch→
272   |  /* trigger a palette change */
273   |  ret = avio_read(pb, palette_buffer, 768);
274   |  if (ret < 0) {
    9←Assuming 'ret' is >= 0→
    10←Taking false branch→
275   |  return ret;
276   |             } else if (ret != 768) {
    11←Assuming 'ret' is equal to 768→
    12←Taking false branch→
277   |                 av_log(s, AV_LOG_ERROR, "incomplete packet\n");
278   |  return AVERROR_INVALIDDATA;
279   |             }
280   |  /* scale the palette as necessary */
281   |  palette_scale = 2;
282   |  for (i = 0; i < 768; i++)
    13←Loop condition is true.  Entering loop body→
283   |  if (palette_buffer[i] > 63) {
    14←Assuming the condition is true→
    15←Taking true branch→
284   |  palette_scale = 0;
285   |  break;
286   |                 }
287   |  
288   |  for (i = 0; i < 256; i++) {
    16← Execution continues on line 288→
    17←Loop condition is true.  Entering loop body→
289   |  r = palette_buffer[i * 3    ] << palette_scale;
    18←buffer read by avio_read may be partially uninitialized
290   |                 g = palette_buffer[i * 3 + 1] << palette_scale;
291   |                 b = palette_buffer[i * 3 + 2] << palette_scale;
292   |                 palette[i] = (0xFFU << 24) | (r << 16) | (g << 8) | (b);
293   |  if (palette_scale == 2)
294   |                     palette[i] |= palette[i] >> 6 & 0x30303;
295   |             }
296   |         }
297   |  
298   |  if (s->pb->eof_reached) {
299   |             av_log(s, AV_LOG_ERROR, "incomplete packet\n");
300   |  return s->pb->error ? s->pb->error : AVERROR_EOF;
301   |         }
302   |         chunk_size = avio_rl32(pb);
303   |  if (chunk_size < 4 || chunk_size > INT_MAX - 4) {
304   |             av_log(s, AV_LOG_ERROR, "invalid chunk size: %u\n", chunk_size);
305   |  return AVERROR_INVALIDDATA;
306   |         }
307   |  /* skip the number of decoded bytes (always equal to width * height) */
308   |         avio_skip(pb, 4);
309   |         chunk_size -= 4;
310   |         ret= av_get_packet(pb, pkt, chunk_size);
311   |  if (ret < 0)
312   |  return ret;
313   |  else if (ret != chunk_size) {
314   |             av_log(s, AV_LOG_ERROR, "incomplete packet\n");
315   |  return AVERROR_INVALIDDATA;
316   |         }
317   |  if (command == 1) {
318   |             uint8_t *pal;
319   |  