### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/nuv.c  
---|---  
Warning:| line 265, column 21  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


199   |  return AVERROR(ENOMEM);
200   |         ctx->v_id = vst->index;
201   |  
202   |         ret = av_image_check_size(width, height, 0, s);
203   |  if (ret < 0)
204   |  return ret;
205   |  
206   |         vst->codecpar->codec_type            = AVMEDIA_TYPE_VIDEO;
207   |         vst->codecpar->codec_id              = AV_CODEC_ID_NUV;
208   |         vst->codecpar->width                 = width;
209   |         vst->codecpar->height                = height;
210   |         vst->codecpar->bits_per_coded_sample = 10;
211   |         vst->sample_aspect_ratio          = av_d2q(aspect * height / width,
212   |                                                    10000);
213   | #if FF_API_R_FRAME_RATE
214   |         vst->r_frame_rate =
215   | #endif
216   |         vst->avg_frame_rate = av_d2q(fps, 60000);
217   |         avpriv_set_pts_info(vst, 32, 1, 1000);
218   |     } else
219   |         ctx->v_id = -1;
220   |  
221   |  if (a_packs) {
222   |         ast = avformat_new_stream(s, NULL);
223   |  if (!ast)
224   |  return AVERROR(ENOMEM);
225   |         ctx->a_id = ast->index;
226   |  
227   |         ast->codecpar->codec_type            = AVMEDIA_TYPE_AUDIO;
228   |         ast->codecpar->codec_id              = AV_CODEC_ID_PCM_S16LE;
229   |         ast->codecpar->channels              = 2;
230   |         ast->codecpar->channel_layout        = AV_CH_LAYOUT_STEREO;
231   |         ast->codecpar->sample_rate           = 44100;
232   |         ast->codecpar->bit_rate              = 2 * 2 * 44100 * 8;
233   |         ast->codecpar->block_align           = 2 * 2;
234   |         ast->codecpar->bits_per_coded_sample = 16;
235   |         avpriv_set_pts_info(ast, 32, 1, 1000);
236   |     } else
237   |         ctx->a_id = -1;
238   |  
239   |  if ((ret = get_codec_data(s, pb, vst, ast, is_mythtv)) < 0)
240   |  return ret;
241   |  
242   |     ctx->rtjpg_video = vst && vst->codecpar->codec_id == AV_CODEC_ID_NUV;
243   |  
244   |  return 0;
245   | }
246   |  
247   | #define HDRSIZE 12
248   |  
249   | static int nuv_packet(AVFormatContext *s, AVPacket *pkt)
250   | {
251   |  NUVContext *ctx = s->priv_data;
252   |     AVIOContext *pb = s->pb;
253   |     uint8_t hdr[HDRSIZE];
254   |     nuv_frametype frametype;
255   |  int ret, size;
256   |  
257   |  while (!avio_feof(pb)) {
    1Assuming the condition is true→
258   |  int copyhdrsize = ctx->rtjpg_video ? HDRSIZE : 0;
    2←Loop condition is true.  Entering loop body→
    3←Assuming field 'rtjpg_video' is 0→
    4←'?' condition is false→
259   |         uint64_t pos    = avio_tell(pb);
260   |  
261   |         ret = avio_read(pb, hdr, HDRSIZE);
262   |  if (ret < HDRSIZE)
    5←Assuming 'ret' is >= HDRSIZE→
    6←Taking false branch→
263   |  return ret < 0 ? ret : AVERROR(EIO);
264   |  
265   |  frametype = hdr[0];
    7←buffer read by avio_read may be partially uninitialized
266   |         size      = PKTSIZE(AV_RL32(&hdr[8]));
267   |  
268   |  switch (frametype) {
269   |  case NUV_EXTRADATA:
270   |  if (!ctx->rtjpg_video) {
271   |                 avio_skip(pb, size);
272   |  break;
273   |             }
274   |  case NUV_VIDEO:
275   |  if (ctx->v_id < 0) {
276   |                 av_log(s, AV_LOG_ERROR, "Video packet in file without video stream!\n");
277   |                 avio_skip(pb, size);
278   |  break;
279   |             }
280   |             ret = av_new_packet(pkt, copyhdrsize + size);
281   |  if (ret < 0)
282   |  return ret;
283   |  
284   |             pkt->pos          = pos;
285   |             pkt->flags       |= hdr[2] == 0 ? AV_PKT_FLAG_KEY : 0;
286   |             pkt->pts          = AV_RL32(&hdr[4]);
287   |             pkt->stream_index = ctx->v_id;
288   |             memcpy(pkt->data, hdr, copyhdrsize);
289   |             ret = avio_read(pb, pkt->data + copyhdrsize, size);
290   |  if (ret < 0) {
291   |  return ret;
292   |             }
293   |  if (ret < size)
294   |                 av_shrink_packet(pkt, copyhdrsize + ret);
295   |  return 0;