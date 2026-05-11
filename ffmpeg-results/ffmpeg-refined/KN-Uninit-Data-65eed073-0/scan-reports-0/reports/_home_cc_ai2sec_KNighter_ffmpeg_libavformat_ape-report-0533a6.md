### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/ape.c  
---|---  
Warning:| line 301, column 13  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


94    | }
95    |  
96    | static void ape_dumpinfo(AVFormatContext * s, APEContext * ape_ctx)
97    | {
98    | #ifdef DEBUG
99    |  int i;
100   |  
101   |     av_log(s, AV_LOG_DEBUG, "Descriptor Block:\n\n");
102   |     av_log(s, AV_LOG_DEBUG, "fileversion          = %"PRId16"\n", ape_ctx->fileversion);
103   |     av_log(s, AV_LOG_DEBUG, "descriptorlength     = %"PRIu32"\n", ape_ctx->descriptorlength);
104   |     av_log(s, AV_LOG_DEBUG, "headerlength         = %"PRIu32"\n", ape_ctx->headerlength);
105   |     av_log(s, AV_LOG_DEBUG, "seektablelength      = %"PRIu32"\n", ape_ctx->seektablelength);
106   |     av_log(s, AV_LOG_DEBUG, "wavheaderlength      = %"PRIu32"\n", ape_ctx->wavheaderlength);
107   |     av_log(s, AV_LOG_DEBUG, "audiodatalength      = %"PRIu32"\n", ape_ctx->audiodatalength);
108   |     av_log(s, AV_LOG_DEBUG, "audiodatalength_high = %"PRIu32"\n", ape_ctx->audiodatalength_high);
109   |     av_log(s, AV_LOG_DEBUG, "wavtaillength        = %"PRIu32"\n", ape_ctx->wavtaillength);
110   |     av_log(s, AV_LOG_DEBUG, "md5                  = ");
111   |  for (i = 0; i < 16; i++)
112   |          av_log(s, AV_LOG_DEBUG, "%02x", ape_ctx->md5[i]);
113   |     av_log(s, AV_LOG_DEBUG, "\n");
114   |  
115   |     av_log(s, AV_LOG_DEBUG, "\nHeader Block:\n\n");
116   |  
117   |     av_log(s, AV_LOG_DEBUG, "compressiontype      = %"PRIu16"\n", ape_ctx->compressiontype);
118   |     av_log(s, AV_LOG_DEBUG, "formatflags          = %"PRIu16"\n", ape_ctx->formatflags);
119   |     av_log(s, AV_LOG_DEBUG, "blocksperframe       = %"PRIu32"\n", ape_ctx->blocksperframe);
120   |     av_log(s, AV_LOG_DEBUG, "finalframeblocks     = %"PRIu32"\n", ape_ctx->finalframeblocks);
121   |     av_log(s, AV_LOG_DEBUG, "totalframes          = %"PRIu32"\n", ape_ctx->totalframes);
122   |     av_log(s, AV_LOG_DEBUG, "bps                  = %"PRIu16"\n", ape_ctx->bps);
123   |     av_log(s, AV_LOG_DEBUG, "channels             = %"PRIu16"\n", ape_ctx->channels);
124   |     av_log(s, AV_LOG_DEBUG, "samplerate           = %"PRIu32"\n", ape_ctx->samplerate);
125   |  
126   |     av_log(s, AV_LOG_DEBUG, "\nSeektable\n\n");
127   |  if ((ape_ctx->seektablelength / sizeof(uint32_t)) != ape_ctx->totalframes) {
128   |         av_log(s, AV_LOG_DEBUG, "No seektable\n");
129   |     }
130   |  
131   |     av_log(s, AV_LOG_DEBUG, "\nFrames\n\n");
132   |  for (i = 0; i < ape_ctx->totalframes; i++)
133   |         av_log(s, AV_LOG_DEBUG, "%8d   %8"PRId64" %8d (%d samples)\n", i,
134   |                ape_ctx->frames[i].pos, ape_ctx->frames[i].size,
135   |                ape_ctx->frames[i].nblocks);
136   |  
137   |     av_log(s, AV_LOG_DEBUG, "\nCalculated information:\n\n");
138   |     av_log(s, AV_LOG_DEBUG, "junklength           = %"PRIu32"\n", ape_ctx->junklength);
139   |     av_log(s, AV_LOG_DEBUG, "firstframe           = %"PRIu32"\n", ape_ctx->firstframe);
140   |     av_log(s, AV_LOG_DEBUG, "totalsamples         = %"PRIu32"\n", ape_ctx->totalsamples);
141   | #endif
142   | }
143   |  
144   | static int ape_read_header(AVFormatContext * s)
145   | {
146   |  AVIOContext *pb = s->pb;
147   |     APEContext *ape = s->priv_data;
148   |     AVStream *st;
149   |     uint32_t tag;
150   |  int i, ret;
151   |  int total_blocks, final_size = 0;
152   |     int64_t pts, file_size;
153   |  
154   |  /* Skip any leading junk such as id3v2 tags */
155   |     ape->junklength = avio_tell(pb);
156   |  
157   |     tag = avio_rl32(pb);
158   |  if (tag != MKTAG('M', 'A', 'C', ' '))
    1Assuming the condition is false→
    2←Taking false branch→
159   |  return AVERROR_INVALIDDATA;
160   |  
161   |  ape->fileversion = avio_rl16(pb);
162   |  
163   |  if (ape->fileversion < APE_MIN_VERSION || ape->fileversion > APE_MAX_VERSION) {
    3←Assuming field 'fileversion' is >= APE_MIN_VERSION→
    4←Assuming field 'fileversion' is <= APE_MAX_VERSION→
    5←Taking false branch→
164   |         av_log(s, AV_LOG_ERROR, "Unsupported file version - %d.%02d\n",
165   |                ape->fileversion / 1000, (ape->fileversion % 1000) / 10);
166   |  return AVERROR_PATCHWELCOME;
167   |     }
168   |  
169   |  if (ape->fileversion >= 3980) {
    6←Assuming field 'fileversion' is >= 3980→
    7←Taking true branch→
170   |  ape->padding1             = avio_rl16(pb);
171   |         ape->descriptorlength     = avio_rl32(pb);
172   |         ape->headerlength         = avio_rl32(pb);
173   |         ape->seektablelength      = avio_rl32(pb);
174   |         ape->wavheaderlength      = avio_rl32(pb);
175   |         ape->audiodatalength      = avio_rl32(pb);
176   |         ape->audiodatalength_high = avio_rl32(pb);
177   |         ape->wavtaillength        = avio_rl32(pb);
178   |         avio_read(pb, ape->md5, 16);
179   |  
180   |  /* Skip any unknown bytes at the end of the descriptor.
181   |  This is for future compatibility */
182   |  if (ape->descriptorlength > 52)
    8←Assuming field 'descriptorlength' is <= 52→
    9←Taking false branch→
183   |             avio_skip(pb, ape->descriptorlength - 52);
184   |  
185   |  /* Read header data */
186   |  ape->compressiontype      = avio_rl16(pb);
187   |         ape->formatflags          = avio_rl16(pb);
188   |         ape->blocksperframe       = avio_rl32(pb);
189   |         ape->finalframeblocks     = avio_rl32(pb);
190   |         ape->totalframes          = avio_rl32(pb);
191   |         ape->bps                  = avio_rl16(pb);
192   |         ape->channels             = avio_rl16(pb);
193   |  ape->samplerate           = avio_rl32(pb);
194   |     } else {
195   |         ape->descriptorlength = 0;
196   |         ape->headerlength = 32;
197   |  
198   |         ape->compressiontype      = avio_rl16(pb);
199   |         ape->formatflags          = avio_rl16(pb);
200   |         ape->channels             = avio_rl16(pb);
201   |         ape->samplerate           = avio_rl32(pb);
202   |         ape->wavheaderlength      = avio_rl32(pb);
203   |         ape->wavtaillength        = avio_rl32(pb);
204   |         ape->totalframes          = avio_rl32(pb);
205   |         ape->finalframeblocks     = avio_rl32(pb);
206   |  
207   |  if (ape->formatflags & MAC_FORMAT_FLAG_HAS_PEAK_LEVEL) {
208   |             avio_skip(pb, 4); /* Skip the peak level */
209   |             ape->headerlength += 4;
210   |         }
211   |  
212   |  if (ape->formatflags & MAC_FORMAT_FLAG_HAS_SEEK_ELEMENTS) {
213   |             ape->seektablelength = avio_rl32(pb);
214   |             ape->headerlength += 4;
215   |             ape->seektablelength *= sizeof(int32_t);
216   |         } else
217   |             ape->seektablelength = ape->totalframes * sizeof(int32_t);
218   |  
219   |  if (ape->formatflags & MAC_FORMAT_FLAG_8_BIT)
220   |             ape->bps = 8;
221   |  else if (ape->formatflags & MAC_FORMAT_FLAG_24_BIT)
222   |             ape->bps = 24;
223   |  else
224   |             ape->bps = 16;
225   |  
226   |  if (ape->fileversion >= 3950)
227   |             ape->blocksperframe = 73728 * 4;
228   |  else if (ape->fileversion >= 3900 || (ape->fileversion >= 3800  && ape->compressiontype >= 4000))
229   |             ape->blocksperframe = 73728;
230   |  else
231   |             ape->blocksperframe = 9216;
232   |  
233   |  /* Skip any stored wav header */
234   |  if (!(ape->formatflags & MAC_FORMAT_FLAG_CREATE_WAV_HEADER))
235   |             avio_skip(pb, ape->wavheaderlength);
236   |     }
237   |  
238   |  if(!ape->totalframes || pb->eof_reached){
    10←Assuming field 'totalframes' is not equal to 0→
    11←Assuming field 'eof_reached' is 0→
    12←Taking false branch→
239   |         av_log(s, AV_LOG_ERROR, "No frames in the file!\n");
240   |  return AVERROR(EINVAL);
241   |     }
242   |  if(ape->totalframes > UINT_MAX / sizeof(APEFrame)){
    13←Assuming the condition is false→
    14←Taking false branch→
243   |         av_log(s, AV_LOG_ERROR, "Too many frames: %"PRIu32"\n",
244   |                ape->totalframes);
245   |  return AVERROR_INVALIDDATA;
246   |     }
247   |  if (ape->seektablelength / sizeof(uint32_t) < ape->totalframes) {
    15←Assuming the condition is false→
    16←Taking false branch→
248   |         av_log(s, AV_LOG_ERROR,
249   |  "Number of seek entries is less than number of frames: %"SIZE_SPECIFIER" vs. %"PRIu32"\n",
250   |                ape->seektablelength / sizeof(uint32_t), ape->totalframes);
251   |  return AVERROR_INVALIDDATA;
252   |     }
253   |  ape->frames       = av_malloc_array(ape->totalframes, sizeof(APEFrame));
254   |  if(!ape->frames)
    17←Assuming field 'frames' is non-null→
    18←Taking false branch→
255   |  return AVERROR(ENOMEM);
256   |  ape->firstframe   = ape->junklength + ape->descriptorlength + ape->headerlength + ape->seektablelength + ape->wavheaderlength;
257   |  if (ape->fileversion < 3810)
    19←Assuming field 'fileversion' is >= 3810→
    20←Taking false branch→
258   |         ape->firstframe += ape->totalframes;
259   |  ape->currentframe = 0;
260   |  
261   |  
262   |     ape->totalsamples = ape->finalframeblocks;
263   |  if (ape->totalframes > 1)
    21←Assuming field 'totalframes' is > 1→
    22←Taking true branch→
264   |  ape->totalsamples += ape->blocksperframe * (ape->totalframes - 1);
265   |  
266   |  ape->frames[0].pos     = ape->firstframe;
267   |     ape->frames[0].nblocks = ape->blocksperframe;
268   |     ape->frames[0].skip    = 0;
269   |     avio_rl32(pb); // seektable[0]
270   |  for (i = 1; i22.1'i' is < field 'totalframes' < ape->totalframes; i++) {
    23←Loop condition is true.  Entering loop body→
    28←Assuming 'i' is >= field 'totalframes'→
    29←Loop condition is false. Execution continues on line 284→
271   |  uint32_t seektable_entry = avio_rl32(pb);
272   |         ape->frames[i].pos      = seektable_entry + ape->junklength;
273   |         ape->frames[i].nblocks  = ape->blocksperframe;
274   |         ape->frames[i - 1].size = ape->frames[i].pos - ape->frames[i - 1].pos;
275   |         ape->frames[i].skip     = (ape->frames[i].pos - ape->frames[0].pos) & 3;
276   |  
277   |  if (pb->eof_reached) {
    24←Assuming field 'eof_reached' is 0→
    25←Taking false branch→
278   |             av_log(s, AV_LOG_ERROR, "seektable truncated\n");
279   |             ret = AVERROR_INVALIDDATA;
280   |  goto fail;
281   |         }
282   |  ff_dlog(s, "seektable: %8d   %"PRIu32"\n", i, seektable_entry);
    26←Taking false branch→
    27←Loop condition is false.  Exiting loop→
283   |  }
284   |  avio_skip(pb, ape->seektablelength / sizeof(uint32_t) - ape->totalframes);
285   |  
286   |     ape->frames[ape->totalframes - 1].nblocks = ape->finalframeblocks;
287   |  /* calculate final packet size from total file size, if available */
288   |     file_size = avio_size(pb);
289   |  if (file_size > 0) {
    30←Assuming 'file_size' is <= 0→
290   |         final_size = file_size - ape->frames[ape->totalframes - 1].pos -
291   |                      ape->wavtaillength;
292   |         final_size -= final_size & 3;
293   |     }
294   |  if (file_size30.1'file_size' is <= 0 <= 0 || final_size <= 0)
295   |  final_size = ape->finalframeblocks * 8;
296   |  ape->frames[ape->totalframes - 1].size = final_size;
297   |  
298   |  for (i = 0; i < ape->totalframes; i++) {
    31←Loop condition is true.  Entering loop body→
    33←Loop condition is true.  Entering loop body→
299   |  if(ape->frames[i].skip31.1Field 'skip' is 0){
    32←Taking false branch→
    34←Assuming field 'skip' is not equal to 0→
    35←Taking true branch→
300   |  ape->frames[i].pos  -= ape->frames[i].skip;
301   |  ape->frames[i].size += ape->frames[i].skip;
    36←buffer read by avio_read may be partially uninitialized
302   |         }
303   |  ape->frames[i].size = (ape->frames[i].size + 3) & ~3;
304   |  }
305   |  if (ape->fileversion < 3810) {
306   |  for (i = 0; i < ape->totalframes; i++) {
307   |  int bits = avio_r8(pb);
308   |  if (i && bits)
309   |                 ape->frames[i - 1].size += 4;
310   |  
311   |             ape->frames[i].skip <<= 3;
312   |             ape->frames[i].skip  += bits;
313   |  ff_dlog(s, "bittable: %2d\n", bits);
314   |  if (pb->eof_reached) {
315   |                 av_log(s, AV_LOG_ERROR, "bittable truncated\n");
316   |                 ret = AVERROR_INVALIDDATA;
317   |  goto fail;
318   |             }
319   |         }
320   |     }
321   |  
322   |     ape_dumpinfo(s, ape);
323   |  
324   |     av_log(s, AV_LOG_VERBOSE, "Decoding file - v%d.%02d, compression level %"PRIu16"\n",
325   |            ape->fileversion / 1000, (ape->fileversion % 1000) / 10,
326   |            ape->compressiontype);
327   |  
328   |  /* now we are ready: build format streams */
329   |     st = avformat_new_stream(s, NULL);
330   |  if (!st) {
331   |         ret = AVERROR(ENOMEM);
332   |  goto fail;
333   |     }
334   |  