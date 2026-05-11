### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/sbgdec.c  
---|---  
Warning:| line 227, column 43  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


173   |     memset(ret, 0, elsize);
174   |     (*size)++;
175   |  return ret;
176   | }
177   |  
178   | static int str_to_time(const char *str, int64_t *rtime)
179   | {
180   |  const char *cur = str;
181   |  char *end;
182   |  int hours, minutes;
183   |  double seconds = 0;
184   |     int64_t ts = 0;
185   |  
186   |  if (*cur < '0' || *cur > '9')
187   |  return 0;
188   |     hours = strtol(cur, &end, 10);
189   |  if (end == cur || *end != ':' || end[1] < '0' || end[1] > '9')
190   |  return 0;
191   |     cur = end + 1;
192   |     minutes = strtol(cur, &end, 10);
193   |  if (end == cur)
194   |  return 0;
195   |     cur = end;
196   |  if (*end == ':'){
197   |         seconds = strtod(cur + 1, &end);
198   |  if (end > cur + 1)
199   |             cur = end;
200   |         ts = av_clipd(seconds * AV_TIME_BASE, INT64_MIN/2, INT64_MAX/2);
201   |     }
202   |     *rtime = av_sat_add64((hours * 3600LL + minutes * 60LL) * AV_TIME_BASE, ts);
203   |  return cur - str;
204   | }
205   |  
206   | static inline int is_space(char c)
207   | {
208   |  return c == ' '  || c == '\t' || c == '\r';
209   | }
210   |  
211   | static inline int scale_double(void *log, double d, double m, int *r)
212   | {
213   |     m *= d * SBG_SCALE;
214   |  if (m < INT_MIN || m >= INT_MAX) {
215   |  if (log)
216   |             av_log(log, AV_LOG_ERROR, "%g is too large\n", d);
217   |  return AVERROR(EDOM);
218   |     }
219   |     *r = m;
220   |  return 0;
221   | }
222   |  
223   | static int lex_space(struct sbg_parser *p)
224   | {
225   |  char *c = p->cursor;
226   |  
227   |  while (p->cursor < p->end && is_space(*p->cursor))
    4←Assuming field 'cursor' is < field 'end'→
    5←buffer read by avio_read may be partially uninitialized
228   |         p->cursor++;
229   |  return p->cursor > c;
230   | }
231   |  
232   | static int lex_char(struct sbg_parser *p, char c)
233   | {
234   |  int r = p->cursor < p->end && *p->cursor == c;
235   |  
236   |     p->cursor += r;
237   |  return r;
238   | }
239   |  
240   | static int lex_double(struct sbg_parser *p, double *r)
241   | {
242   |  double d;
243   |  char *end;
244   |  
245   |  if (p->cursor == p->end || is_space(*p->cursor) || *p->cursor == '\n')
246   |  return 0;
247   |     d = strtod(p->cursor, &end);
248   |  if (end > p->cursor) {
249   |         *r = d;
250   |         p->cursor = end;
251   |  return 1;
252   |     }
253   |  return 0;
254   | }
255   |  
256   | static int lex_fixed(struct sbg_parser *p, const char *t, int l)
257   | {
749   |  break;
750   |     }
751   |     lex_space(p);
752   |  if (synth == p->scs.nb_synth)
753   |  return AVERROR_INVALIDDATA;
754   |  if (!lex_line_end(p))
755   |  return AVERROR_INVALIDDATA;
756   |     def->type        = 'S';
757   |     def->elements    = synth;
758   |     def->nb_elements = p->scs.nb_synth - synth;
759   |  return 1;
760   | }
761   |  
762   | static int parse_named_def(struct sbg_parser *p)
763   | {
764   |  char *cursor_save = p->cursor;
765   |  struct sbg_string name;
766   |  struct sbg_script_definition *def;
767   |  
768   |  if (!lex_name(p, &name) || !lex_char(p, ':') || !lex_space(p)) {
769   |         p->cursor = cursor_save;
770   |  return 0;
771   |     }
772   |  if (name.e - name.s == 6 && !memcmp(name.s, "wave", 4) &&
773   |         name.s[4] >= '0' && name.s[4] <= '9' &&
774   |         name.s[5] >= '0' && name.s[5] <= '9') {
775   |  int wavenum = (name.s[4] - '0') * 10 + (name.s[5] - '0');
776   |  return parse_wave_def(p, wavenum);
777   |     }
778   |     def = alloc_array_elem((void **)&p->scs.def, sizeof(*def),
779   |                            &p->scs.nb_def, &p->nb_def_max);
780   |  if (!def)
781   |  return AVERROR(ENOMEM);
782   |     def->name     = name.s;
783   |     def->name_len = name.e - name.s;
784   |  if (lex_char(p, '{'))
785   |  return parse_block_def(p, def);
786   |  return parse_synth_def(p, def);
787   | }
788   |  
789   | static void free_script(struct sbg_script *s)
790   | {
791   |     av_freep(&s->def);
792   |     av_freep(&s->synth);
793   |     av_freep(&s->tseq);
794   |     av_freep(&s->block_tseq);
795   |     av_freep(&s->events);
796   |     av_freep(&s->opt_mix);
797   | }
798   |  
799   | static int parse_script(void *log, char *script, int script_len,
800   |  struct sbg_script *rscript)
801   | {
802   |  struct sbg_parser sp = {
803   |         .log     = log,
804   |         .script  = script,
805   |         .end     = script + script_len,
806   |         .cursor  = script,
807   |         .line_no = 1,
808   |         .err_msg = "",
809   |         .scs = {
810   |  /* default values */
811   |             .start_ts      = AV_NOPTS_VALUE,
812   |             .sample_rate   = 44100,
813   |             .opt_fade_time = 60 * AV_TIME_BASE,
814   |         },
815   |     };
816   |  int r;
817   |  
818   |  lex_space(&sp);
    3←Calling 'lex_space'→
819   |  while (sp.cursor < sp.end) {
820   |         r = parse_options(&sp);
821   |  if (r < 0)
822   |  goto fail;
823   |  if (!r && !lex_line_end(&sp))
824   |  break;
825   |     }
826   |  while (sp.cursor < sp.end) {
827   |         r = parse_named_def(&sp);
828   |  if (!r)
829   |             r = parse_time_sequence(&sp, 0);
830   |  if (!r)
831   |             r = lex_line_end(&sp) ? 1 : AVERROR_INVALIDDATA;
832   |  if (r < 0)
833   |  goto fail;
834   |     }
835   |     *rscript = sp.scs;
836   |  return 1;
837   | fail:
838   |     free_script(&sp.scs);
839   |  if (!*sp.err_msg)
840   |  if (r == AVERROR_INVALIDDATA)
841   |             snprintf(sp.err_msg, sizeof(sp.err_msg), "syntax error");
842   |  if (log && *sp.err_msg) {
843   |  const char *ctx = sp.cursor;
844   |  const char *ectx = av_x_if_null(memchr(ctx, '\n', sp.end - sp.cursor),
845   |                                         sp.end);
846   |  int lctx = ectx - ctx;
847   |  const char *quote = "\"";
848   |  if (lctx > 0 && ctx[lctx - 1] == '\r')
849   |             lctx--;
850   |  if (lctx == 0) {
851   |             ctx = "the end of line";
852   |             lctx = strlen(ctx);
853   |             quote = "";
854   |         }
855   |         av_log(log, AV_LOG_ERROR, "Error line %d: %s near %s%.*s%s.\n",
856   |                sp.line_no, sp.err_msg, quote, lctx, ctx, quote);
857   |     }
858   |  return r;
859   | }
860   |  
861   | static int read_whole_file(AVIOContext *io, int max_size, char **rbuf)
862   | {
863   |  char *buf = NULL;
864   |  int size = 0, bufsize = 0, r;
865   |  
866   |  while (1) {
867   |  if (bufsize - size < 1024) {
868   |             bufsize = FFMIN(FFMAX(2 * bufsize, 8192), max_size);
869   |  if (bufsize - size < 2) {
870   |                 size = AVERROR(EFBIG);
871   |  goto fail;
872   |             }
873   |             buf = av_realloc_f(buf, bufsize, 1);
874   |  if (!buf) {
875   |                 size = AVERROR(ENOMEM);
876   |  goto fail;
877   |             }
878   |         }
879   |         r = avio_read(io, buf, bufsize - size - 1);
880   |  if (r == AVERROR_EOF)
881   |  break;
882   |  if (r < 0)
883   |  goto fail;
884   |         size += r;
885   |     }
886   |     buf[size] = 0;
887   |     *rbuf = buf;
888   |  return size;
889   | fail:
890   |     av_free(buf);
891   |  return size;
892   | }
893   |  
894   | static int expand_timestamps(void *log, struct sbg_script *s)
895   | {
896   |  int i, nb_rel = 0;
897   |     int64_t now, cur_ts, delta = 0;
898   |  
899   |  for (i = 0; i < s->nb_tseq; i++)
900   |         nb_rel += s->tseq[i].ts.type == 'N';
901   |  if (nb_rel == s->nb_tseq) {
902   |  /* All ts are relative to NOW: consider NOW = 0 */
903   |         now = 0;
904   |  if (s->start_ts != AV_NOPTS_VALUE)
905   |             av_log(log, AV_LOG_WARNING,
906   |  "Start time ignored in a purely relative script.\n");
907   |     } else if (nb_rel == 0 && s->start_ts != AV_NOPTS_VALUE ||
908   |                s->opt_start_at_first) {
909   |  /* All ts are absolute and start time is specified */
910   |  if (s->start_ts == AV_NOPTS_VALUE)
911   |             s->start_ts = s->tseq[0].ts.t;
912   |         now = s->start_ts;
913   |     } else {
914   |  /* Mixed relative/absolute ts: expand */
915   |         time_t now0;
916   |  struct tm *tm, tmpbuf;
917   |  
918   |         av_log(log, AV_LOG_WARNING,
1344  |  
1345  |  for (i = 0; i < inter->nb_inter; i++) {
1346  |         edata_size += inter->inter[i].type == WS_SINE  ? 44 :
1347  |                       inter->inter[i].type == WS_NOISE ? 32 : 0;
1348  |  if (edata_size < 0)
1349  |  return AVERROR(ENOMEM);
1350  |     }
1351  |  if ((ret = ff_alloc_extradata(par, edata_size)) < 0)
1352  |  return ret;
1353  |     edata = par->extradata;
1354  |  
1355  | #define ADD_EDATA32(v) do { AV_WL32(edata, (v)); edata += 4; } while(0)
1356  | #define ADD_EDATA64(v) do { AV_WL64(edata, (v)); edata += 8; } while(0)
1357  |  ADD_EDATA32(inter->nb_inter);
1358  |  for (i = 0; i < inter->nb_inter; i++) {
1359  |  ADD_EDATA64(inter->inter[i].ts1);
1360  |  ADD_EDATA64(inter->inter[i].ts2);
1361  |  ADD_EDATA32(inter->inter[i].type);
1362  |  ADD_EDATA32(inter->inter[i].channels);
1363  |  switch (inter->inter[i].type) {
1364  |  case WS_SINE:
1365  |  ADD_EDATA32(inter->inter[i].f1);
1366  |  ADD_EDATA32(inter->inter[i].f2);
1367  |  ADD_EDATA32(inter->inter[i].a1);
1368  |  ADD_EDATA32(inter->inter[i].a2);
1369  |  ADD_EDATA32(inter->inter[i].phi);
1370  |  break;
1371  |  case WS_NOISE:
1372  |  ADD_EDATA32(inter->inter[i].a1);
1373  |  ADD_EDATA32(inter->inter[i].a2);
1374  |  break;
1375  |         }
1376  |     }
1377  |  if (edata != par->extradata + edata_size)
1378  |  return AVERROR_BUG;
1379  |  return 0;
1380  | }
1381  |  
1382  | static av_cold int sbg_read_probe(const AVProbeData *p)
1383  | {
1384  |  int r, score;
1385  |  struct sbg_script script = { 0 };
1386  |  
1387  |     r = parse_script(NULL, p->buf, p->buf_size, &script);
1388  |     score = r < 0 || !script.nb_def || !script.nb_tseq ? 0 :
1389  |  AVPROBE_SCORE_MAX / 3;
1390  |     free_script(&script);
1391  |  return score;
1392  | }
1393  |  
1394  | static av_cold int sbg_read_header(AVFormatContext *avf)
1395  | {
1396  |  struct sbg_demuxer *sbg = avf->priv_data;
1397  |  int r;
1398  |  char *buf = NULL;
1399  |  struct sbg_script script = { 0 };
1400  |     AVStream *st;
1401  |  struct ws_intervals inter = { 0 };
1402  |  
1403  |     r = read_whole_file(avf->pb, sbg->max_file_size, &buf);
1404  |  if (r0.1'r' is >= 0 < 0)
    1Taking false branch→
1405  |  goto fail;
1406  |  r = parse_script(avf, buf, r, &script);
    2←Calling 'parse_script'→
1407  |  if (r < 0)
1408  |  goto fail;
1409  |  if (!sbg->sample_rate)
1410  |         sbg->sample_rate = script.sample_rate;
1411  |  else
1412  |         script.sample_rate = sbg->sample_rate;
1413  |  if (!sbg->frame_size)
1414  |         sbg->frame_size = FFMAX(1, sbg->sample_rate / 10);
1415  |  if (script.opt_mix)
1416  |         av_log(avf, AV_LOG_WARNING, "Mix feature not implemented: "
1417  |  "-m is ignored and mix channels will be silent.\n");
1418  |     r = expand_script(avf, &script);
1419  |  if (r < 0)
1420  |  goto fail;
1421  |     av_freep(&buf);
1422  |     r = generate_intervals(avf, &script, sbg->sample_rate, &inter);
1423  |  if (r < 0)
1424  |  goto fail;
1425  |  
1426  |  if (script.end_ts != AV_NOPTS_VALUE && script.end_ts < script.start_ts) {
1427  |         r = AVERROR_INVALIDDATA;
1428  |  goto fail;
1429  |     }
1430  |  
1431  |     st = avformat_new_stream(avf, NULL);
1432  |  if (!st)
1433  |  return AVERROR(ENOMEM);
1434  |     st->codecpar->codec_type     = AVMEDIA_TYPE_AUDIO;
1435  |     st->codecpar->codec_id       = AV_CODEC_ID_FFWAVESYNTH;
1436  |     st->codecpar->channels       = 2;