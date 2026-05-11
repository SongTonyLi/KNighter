### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/./libavutil/bprint.h  
---|---  
Warning:| line 220, column 23  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


815   |             .start_ts      = AV_NOPTS_VALUE,
816   |             .sample_rate   = 44100,
817   |             .opt_fade_time = 60 * AV_TIME_BASE,
818   |         },
819   |     };
820   |  int r;
821   |  
822   |     lex_space(&sp);
823   |  while (sp.cursor < sp.end) {
824   |         r = parse_options(&sp);
825   |  if (r < 0)
826   |  goto fail;
827   |  if (!r && !lex_line_end(&sp))
828   |  break;
829   |     }
830   |  while (sp.cursor < sp.end) {
831   |         r = parse_named_def(&sp);
832   |  if (!r)
833   |             r = parse_time_sequence(&sp, 0);
834   |  if (!r)
835   |             r = lex_line_end(&sp) ? 1 : AVERROR_INVALIDDATA;
836   |  if (r < 0)
837   |  goto fail;
838   |     }
839   |     *rscript = sp.scs;
840   |  return 1;
841   | fail:
842   |     free_script(&sp.scs);
843   |  if (!*sp.err_msg)
844   |  if (r == AVERROR_INVALIDDATA)
845   |             snprintf(sp.err_msg, sizeof(sp.err_msg), "syntax error");
846   |  if (log && *sp.err_msg) {
847   |  const char *ctx = sp.cursor;
848   |  const char *ectx = av_x_if_null(memchr(ctx, '\n', sp.end - sp.cursor),
849   |                                         sp.end);
850   |  int lctx = ectx - ctx;
851   |  const char *quote = "\"";
852   |  if (lctx > 0 && ctx[lctx - 1] == '\r')
853   |             lctx--;
854   |  if (lctx == 0) {
855   |             ctx = "the end of line";
856   |             lctx = strlen(ctx);
857   |             quote = "";
858   |         }
859   |         av_log(log, AV_LOG_ERROR, "Error line %d: %s near %s%.*s%s.\n",
860   |                sp.line_no, sp.err_msg, quote, lctx, ctx, quote);
861   |     }
862   |  return r;
863   | }
864   |  
865   | static int read_whole_file(AVIOContext *io, int max_size, AVBPrint *rbuf)
866   | {
867   |  int ret = avio_read_to_bprint(io, rbuf, max_size);
868   |  if (ret < 0)
    2←Assuming 'ret' is >= 0→
    3←Taking false branch→
869   |  return ret;
870   |  if (!av_bprint_is_complete(rbuf))
    4←Calling 'av_bprint_is_complete'→
871   |  return AVERROR(ENOMEM);
872   |  /* Check if we have read the whole file. AVIOContext.eof_reached is only
873   |  * set after a read failed due to EOF, so this check is incorrect in case
874   |  * max_size equals the actual file size, but checking for that would
875   |  * require attempting to read beyond max_size. */
876   |  if (!io->eof_reached)
877   |  return AVERROR(EFBIG);
878   |  return 0;
879   | }
880   |  
881   | static int expand_timestamps(void *log, struct sbg_script *s)
882   | {
883   |  int i, nb_rel = 0;
884   |     int64_t now, cur_ts, delta = 0;
885   |  
886   |  for (i = 0; i < s->nb_tseq; i++)
887   |         nb_rel += s->tseq[i].ts.type == 'N';
888   |  if (nb_rel == s->nb_tseq) {
889   |  /* All ts are relative to NOW: consider NOW = 0 */
890   |         now = 0;
891   |  if (s->start_ts != AV_NOPTS_VALUE)
892   |             av_log(log, AV_LOG_WARNING,
893   |  "Start time ignored in a purely relative script.\n");
894   |     } else if (nb_rel == 0 && s->start_ts != AV_NOPTS_VALUE ||
895   |                s->opt_start_at_first) {
896   |  /* All ts are absolute and start time is specified */
897   |  if (s->start_ts == AV_NOPTS_VALUE)
898   |             s->start_ts = s->tseq[0].ts.t;
899   |         now = s->start_ts;
900   |     } else {
1345  |  
1346  |  for (i = 0; i < inter->nb_inter; i++) {
1347  |         edata_size += inter->inter[i].type == WS_SINE  ? 44 :
1348  |                       inter->inter[i].type == WS_NOISE ? 32 : 0;
1349  |  if (edata_size < 0)
1350  |  return AVERROR(ENOMEM);
1351  |     }
1352  |  if ((ret = ff_alloc_extradata(par, edata_size)) < 0)
1353  |  return ret;
1354  |     edata = par->extradata;
1355  |  
1356  | #define ADD_EDATA32(v) do { AV_WL32(edata, (v)); edata += 4; } while(0)
1357  | #define ADD_EDATA64(v) do { AV_WL64(edata, (v)); edata += 8; } while(0)
1358  |  ADD_EDATA32(inter->nb_inter);
1359  |  for (i = 0; i < inter->nb_inter; i++) {
1360  |  ADD_EDATA64(inter->inter[i].ts1);
1361  |  ADD_EDATA64(inter->inter[i].ts2);
1362  |  ADD_EDATA32(inter->inter[i].type);
1363  |  ADD_EDATA32(inter->inter[i].channels);
1364  |  switch (inter->inter[i].type) {
1365  |  case WS_SINE:
1366  |  ADD_EDATA32(inter->inter[i].f1);
1367  |  ADD_EDATA32(inter->inter[i].f2);
1368  |  ADD_EDATA32(inter->inter[i].a1);
1369  |  ADD_EDATA32(inter->inter[i].a2);
1370  |  ADD_EDATA32(inter->inter[i].phi);
1371  |  break;
1372  |  case WS_NOISE:
1373  |  ADD_EDATA32(inter->inter[i].a1);
1374  |  ADD_EDATA32(inter->inter[i].a2);
1375  |  break;
1376  |         }
1377  |     }
1378  |  if (edata != par->extradata + edata_size)
1379  |  return AVERROR_BUG;
1380  |  return 0;
1381  | }
1382  |  
1383  | static av_cold int sbg_read_probe(const AVProbeData *p)
1384  | {
1385  |  int r, score;
1386  |  struct sbg_script script = { 0 };
1387  |  
1388  |     r = parse_script(NULL, p->buf, p->buf_size, &script);
1389  |     score = r < 0 || !script.nb_def || !script.nb_tseq ? 0 :
1390  |  AVPROBE_SCORE_MAX / 3;
1391  |     free_script(&script);
1392  |  return score;
1393  | }
1394  |  
1395  | static av_cold int sbg_read_header(AVFormatContext *avf)
1396  | {
1397  |  struct sbg_demuxer *sbg = avf->priv_data;
1398  |     AVBPrint bprint;
1399  |  int r;
1400  |  struct sbg_script script = { 0 };
1401  |     AVStream *st;
1402  |     FFStream *sti;
1403  |  struct ws_intervals inter = { 0 };
1404  |  
1405  |     av_bprint_init(&bprint, 0, sbg->max_file_size + 1U);
1406  |  r = read_whole_file(avf->pb, sbg->max_file_size, &bprint);
    1Calling 'read_whole_file'→
1407  |  if (r < 0)
1408  |  goto fail2;
1409  |  
1410  |     r = parse_script(avf, bprint.str, bprint.len, &script);
1411  |  if (r < 0)
1412  |  goto fail2;
1413  |  if (!sbg->sample_rate)
1414  |         sbg->sample_rate = script.sample_rate;
1415  |  else
1416  |         script.sample_rate = sbg->sample_rate;
1417  |  if (!sbg->frame_size)
1418  |         sbg->frame_size = FFMAX(1, sbg->sample_rate / 10);
1419  |  if (script.opt_mix)
1420  |         av_log(avf, AV_LOG_WARNING, "Mix feature not implemented: "
1421  |  "-m is ignored and mix channels will be silent.\n");
1422  |     r = expand_script(avf, &script);
1423  |  if (r < 0)
1424  |  goto fail2;
1425  |     av_bprint_finalize(&bprint, NULL);
1426  |     r = generate_intervals(avf, &script, sbg->sample_rate, &inter);
1427  |  if (r < 0)
1428  |  goto fail;
1429  |  
1430  |  if (script.end_ts != AV_NOPTS_VALUE && script.end_ts < script.start_ts) {
1431  |         r = AVERROR_INVALIDDATA;
1432  |  goto fail;
1433  |     }
1434  |  
1435  |     st = avformat_new_stream(avf, NULL);
1436  |  if (!st) {
168   |  * Append char c n times to a print buffer.
169   |  */
170   | void av_bprint_chars(AVBPrint *buf, char c, unsigned n);
171   |  
172   | /**
173   |  * Append data to a print buffer.
174   |  *
175   |  * @param buf  bprint buffer to use
176   |  * @param data pointer to data
177   |  * @param size size of data
178   |  */
179   | void av_bprint_append_data(AVBPrint *buf, const char *data, unsigned size);
180   |  
181   | struct tm;
182   | /**
183   |  * Append a formatted date and time to a print buffer.
184   |  *
185   |  * @param buf  bprint buffer to use
186   |  * @param fmt  date and time format string, see strftime()
187   |  * @param tm   broken-down time structure to translate
188   |  *
189   |  * @note due to poor design of the standard strftime function, it may
190   |  * produce poor results if the format string expands to a very long text and
191   |  * the bprint buffer is near the limit stated by the size_max option.
192   |  */
193   | void av_bprint_strftime(AVBPrint *buf, const char *fmt, const struct tm *tm);
194   |  
195   | /**
196   |  * Allocate bytes in the buffer for external use.
197   |  *
198   |  * @param[in]  buf          buffer structure
199   |  * @param[in]  size         required size
200   |  * @param[out] mem          pointer to the memory area
201   |  * @param[out] actual_size  size of the memory area after allocation;
202   |  *                          can be larger or smaller than size
203   |  */
204   | void av_bprint_get_buffer(AVBPrint *buf, unsigned size,
205   |  unsigned char **mem, unsigned *actual_size);
206   |  
207   | /**
208   |  * Reset the string to "" but keep internal allocated data.
209   |  */
210   | void av_bprint_clear(AVBPrint *buf);
211   |  
212   | /**
213   |  * Test if the print buffer is complete (not truncated).
214   |  *
215   |  * It may have been truncated due to a memory allocation failure
216   |  * or the size_max limit (compare size and size_max if necessary).
217   |  */
218   | static inline int av_bprint_is_complete(const AVBPrint *buf)
219   | {
220   |  return buf->len < buf->size;
    5←buffer read by avio_read may be partially uninitialized
221   | }
222   |  
223   | /**
224   |  * Finalize a print buffer.
225   |  *
226   |  * The print buffer can no longer be used afterwards,
227   |  * but the len and size fields are still valid.
228   |  *
229   |  * @arg[out] ret_str  if not NULL, used to return a permanent copy of the
230   |  *                    buffer contents, or NULL if memory allocation fails;
231   |  *                    if NULL, the buffer is discarded and freed
232   |  * @return  0 for success or error code (probably AVERROR(ENOMEM))
233   |  */
234   | int av_bprint_finalize(AVBPrint *buf, char **ret_str);
235   |  
236   | /**
237   |  * Escape the content in src and append it to dstbuf.
238   |  *
239   |  * @param dstbuf        already inited destination bprint buffer
240   |  * @param src           string containing the text to escape
241   |  * @param special_chars string containing the special characters which
242   |  *                      need to be escaped, can be NULL
243   |  * @param mode          escape mode to employ, see AV_ESCAPE_MODE_* macros.
244   |  *                      Any unknown value for mode will be considered equivalent to
245   |  *                      AV_ESCAPE_MODE_BACKSLASH, but this behaviour can change without
246   |  *                      notice.
247   |  * @param flags         flags which control how to escape, see AV_ESCAPE_FLAG_* macros
248   |  */
249   | void av_bprint_escape(AVBPrint *dstbuf, const char *src, const char *special_chars,
250   |  enum AVEscapeMode mode, int flags);