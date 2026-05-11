### Report Summary

File:| format/dss.c  
---|---  
Warning:| line 199, column 33  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


137   |  return ret;
138   |  
139   |     ret = dss_read_metadata_string(s, DSS_HEAD_OFFSET_COMMENT,
140   |  DSS_COMMENT_SIZE, "comment");
141   |  if (ret)
142   |  return ret;
143   |  
144   |     avio_seek(pb, DSS_HEAD_OFFSET_ACODEC, SEEK_SET);
145   |     ctx->audio_codec = avio_r8(pb);
146   |  
147   |  if (ctx->audio_codec == DSS_ACODEC_DSS_SP) {
148   |         st->codecpar->codec_id    = AV_CODEC_ID_DSS_SP;
149   |         st->codecpar->sample_rate = 11025;
150   |         s->bit_rate = 8 * (DSS_FRAME_SIZE - 1) * st->codecpar->sample_rate
151   |                         * 512 / (506 * 264);
152   |     } else if (ctx->audio_codec == DSS_ACODEC_G723_1) {
153   |         st->codecpar->codec_id    = AV_CODEC_ID_G723_1;
154   |         st->codecpar->sample_rate = 8000;
155   |     } else {
156   |         avpriv_request_sample(s, "Support for codec %x in DSS",
157   |                               ctx->audio_codec);
158   |  return AVERROR_PATCHWELCOME;
159   |     }
160   |  
161   |     st->codecpar->codec_type     = AVMEDIA_TYPE_AUDIO;
162   |     st->codecpar->ch_layout      = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
163   |  
164   |     avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
165   |     st->start_time = 0;
166   |  
167   |  /* Jump over header */
168   |  
169   |  if ((ret64 = avio_seek(pb, ctx->dss_header_size, SEEK_SET)) < 0)
170   |  return (int)ret64;
171   |  
172   |     ctx->counter = 0;
173   |     ctx->swap    = 0;
174   |  
175   |  return 0;
176   | }
177   |  
178   | static void dss_skip_audio_header(AVFormatContext *s, AVPacket *pkt)
179   | {
180   |     DSSDemuxContext *ctx = s->priv_data;
181   |     AVIOContext *pb = s->pb;
182   |  
183   |     avio_skip(pb, DSS_AUDIO_BLOCK_HEADER_SIZE);
184   |     ctx->counter += DSS_BLOCK_SIZE - DSS_AUDIO_BLOCK_HEADER_SIZE;
185   | }
186   |  
187   | static void dss_sp_byte_swap(DSSDemuxContext *ctx, uint8_t *data)
188   | {
189   |  int i;
190   |  
191   |  if (ctx->swap14.1Field 'swap' is 0) {
    15←Taking false branch→
192   |  for (i = 0; i < DSS_FRAME_SIZE - 2; i += 2)
193   |             data[i] = data[i + 4];
194   |  
195   |  /* Zero the padding. */
196   |         data[DSS_FRAME_SIZE] = 0;
197   |         data[1] = ctx->dss_sp_swap_byte;
198   |     } else {
199   |  ctx->dss_sp_swap_byte = data[DSS_FRAME_SIZE - 2];
    16←buffer read by avio_read may be partially uninitialized
200   |     }
201   |  
202   |  /* make sure byte 40 is always 0 */
203   |     data[DSS_FRAME_SIZE - 2] = 0;
204   |     ctx->swap             ^= 1;
205   | }
206   |  
207   | static int dss_sp_read_packet(AVFormatContext *s, AVPacket *pkt)
208   | {
209   |  DSSDemuxContext *ctx = s->priv_data;
210   |  int read_size, ret, offset = 0, buff_offset = 0;
211   |     int64_t pos = avio_tell(s->pb);
212   |  
213   |  if (ctx->counter == 0)
    4←Assuming field 'counter' is not equal to 0→
    5←Taking false branch→
214   |         dss_skip_audio_header(s, pkt);
215   |  
216   |  if (ctx->swap) {
    6←Assuming field 'swap' is 0→
    7←Taking false branch→
217   |         read_size   = DSS_FRAME_SIZE - 2;
218   |         buff_offset = 3;
219   |     } else
220   |  read_size = DSS_FRAME_SIZE;
221   |  
222   |  ret = av_new_packet(pkt, DSS_FRAME_SIZE);
223   |  if (ret < 0)
    8←Assuming 'ret' is >= 0→
    9←Taking false branch→
224   |  return ret;
225   |  
226   |  pkt->duration     = 264;
227   |     pkt->pos = pos;
228   |     pkt->stream_index = 0;
229   |  
230   |  if (ctx->counter < read_size) {
    10←Assuming 'read_size' is <= field 'counter'→
    11←Taking false branch→
231   |         ret = avio_read(s->pb, pkt->data + buff_offset,
232   |                         ctx->counter);
233   |  if (ret < ctx->counter)
234   |  goto error_eof;
235   |  
236   |         offset = ctx->counter;
237   |         dss_skip_audio_header(s, pkt);
238   |     }
239   |  ctx->counter -= read_size;
240   |  
241   |  /* This will write one byte into pkt's padding if buff_offset == 3 */
242   |     ret = avio_read(s->pb, pkt->data + offset + buff_offset,
243   |                     read_size - offset);
244   |  if (ret < read_size - offset)
    12←Assuming the condition is false→
    13←Taking false branch→
245   |  goto error_eof;
246   |  
247   |  dss_sp_byte_swap(ctx, pkt->data);
    14←Calling 'dss_sp_byte_swap'→
248   |  
249   |  if (ctx->dss_sp_swap_byte < 0) {
250   |  return AVERROR(EAGAIN);
251   |     }
252   |  
253   |  return 0;
254   |  
255   | error_eof:
256   |  return ret < 0 ? ret : AVERROR_EOF;
257   | }
258   |  
259   | static int dss_723_1_read_packet(AVFormatContext *s, AVPacket *pkt)
260   | {
261   |     DSSDemuxContext *ctx = s->priv_data;
262   |     AVStream *st = s->streams[0];
263   |  int size, byte, ret, offset;
264   |     int64_t pos = avio_tell(s->pb);
265   |  
266   |  if (ctx->counter == 0)
267   |         dss_skip_audio_header(s, pkt);
268   |  
269   |  /* We make one byte-step here. Don't forget to add offset. */
270   |     byte = avio_r8(s->pb);
271   |  if (byte == 0xff)
272   |  return AVERROR_INVALIDDATA;
273   |  
274   |     size = frame_size[byte & 3];
275   |  
276   |     ctx->packet_size = size;
277   |     ctx->counter--;
278   |  
279   |     ret = av_new_packet(pkt, size);
280   |  if (ret < 0)
281   |  return ret;
282   |     pkt->pos = pos;
283   |  
284   |     pkt->data[0]  = byte;
285   |     offset        = 1;
286   |     pkt->duration = 240;
287   |     s->bit_rate = 8LL * size-- * st->codecpar->sample_rate * 512 / (506 * pkt->duration);
288   |  
289   |     pkt->stream_index = 0;
290   |  
291   |  if (ctx->counter < size) {
292   |         ret = avio_read(s->pb, pkt->data + offset,
293   |                         ctx->counter);
294   |  if (ret < ctx->counter)
295   |  return ret < 0 ? ret : AVERROR_EOF;
296   |  
297   |         offset += ctx->counter;
298   |         size   -= ctx->counter;
299   |         ctx->counter = 0;
300   |         dss_skip_audio_header(s, pkt);
301   |     }
302   |     ctx->counter -= size;
303   |  
304   |     ret = avio_read(s->pb, pkt->data + offset, size);
305   |  if (ret < size)
306   |  return ret < 0 ? ret : AVERROR_EOF;
307   |  
308   |  return 0;
309   | }
310   |  
311   | static int dss_read_packet(AVFormatContext *s, AVPacket *pkt)
312   | {
313   |  DSSDemuxContext *ctx = s->priv_data;
314   |  
315   |  if (ctx->audio_codec == DSS_ACODEC_DSS_SP)
    1Assuming field 'audio_codec' is equal to DSS_ACODEC_DSS_SP→
    2←Taking true branch→
316   |  return dss_sp_read_packet(s, pkt);
    3←Calling 'dss_sp_read_packet'→
317   |  else
318   |  return dss_723_1_read_packet(s, pkt);
319   | }
320   |  
321   | static int dss_read_seek(AVFormatContext *s, int stream_index,
322   |                          int64_t timestamp, int flags)
323   | {
324   |     DSSDemuxContext *ctx = s->priv_data;
325   |     int64_t ret, seekto;
326   |     uint8_t header[DSS_AUDIO_BLOCK_HEADER_SIZE];
327   |  int offset;
328   |  
329   |  if (ctx->audio_codec == DSS_ACODEC_DSS_SP)
330   |         seekto = timestamp / 264 * 41 / 506 * 512;
331   |  else
332   |         seekto = timestamp / 240 * ctx->packet_size / 506 * 512;
333   |  
334   |  if (seekto < 0)
335   |         seekto = 0;
336   |  
337   |     seekto += ctx->dss_header_size;
338   |  
339   |     ret = avio_seek(s->pb, seekto, SEEK_SET);
340   |  if (ret < 0)
341   |  return ret;
342   |  
343   |     ret = ffio_read_size(s->pb, header, DSS_AUDIO_BLOCK_HEADER_SIZE);
344   |  if (ret < 0)
345   |  return ret;
346   |     ctx->swap = !!(header[0] & 0x80);