### Report Summary

File:| avfilter/vf_bm3d.c  
---|---  
Warning:| line 598, column 22  
division by possibly zero aggregate factor  
  
### Annotated Source Code


448   |                 }
449   |             }
450   |             bufferz += pgroup_size;
451   |         }
452   |     }
453   |  
454   |     bufferz = sc->bufferz;
455   |     buffer = sc->buffer;
456   |  for (int i = 0; i < block_size; i++) {
457   |  for (int j = 0; j < block_size; j++) {
458   |  if (group_size > 1)
459   |                 sc->itx_fn_g(sc->gdcti, bufferz, bufferz, sizeof(float));
460   |  for (int k = 0; k < nb_match_blocks; k++)
461   |                 buffer[buffer_linesize * k + i * pblock_size + j] = bufferz[k];
462   |             bufferz += pgroup_size;
463   |         }
464   |     }
465   |  
466   |     den_weight = retained < 1 ? 1.f : 1.f / retained;
467   |     num_weight = den_weight;
468   |  
469   |     buffer = sc->buffer;
470   |  for (int k = 0; k < nb_match_blocks; k++) {
471   |  float *num = sc->num + y * width + x;
472   |  float *den = sc->den + y * width + x;
473   |  
474   |  for (int i = 0; i < block_size; i++) {
475   |             memcpy(bufferv + i * pblock_size,
476   |                    buffer + k * buffer_linesize + i * pblock_size,
477   |                    block_size * sizeof(float));
478   |         }
479   |  
480   |  for (int i = 0; i < block_size; i++) {
481   |             sc->itx_fn(sc->dcti, buffert, bufferv + i * pblock_size, sizeof(float));
482   |  for (int j = 0; j < block_size; j++)
483   |                 bufferh[j * pblock_size + i] = buffert[j];
484   |         }
485   |  
486   |  for (int i = 0; i < block_size; i++) {
487   |             sc->itx_fn(sc->dcti, buffert, bufferh + pblock_size * i, sizeof(float));
488   |  for (int j = 0; j < block_size; j++) {
489   |                 num[j] += buffert[j] * num_weight;
490   |                 den[j] += den_weight;
491   |             }
492   |             num += width;
493   |             den += width;
494   |         }
495   |     }
496   | }
497   |  
498   | static void final_block_filtering(BM3DContext *s, const uint8_t *src, int src_linesize,
499   |  const uint8_t *ref, int ref_linesize,
500   |  int y, int x, int plane, int jobnr)
501   | {
502   |  SliceContext *sc = &s->slices[jobnr];
503   |  const int pblock_size = s->pblock_size;
504   |  const int buffer_linesize = s->pblock_size * s->pblock_size;
505   |  const int nb_match_blocks = sc->nb_match_blocks;
506   |  const int block_size = s->block_size;
507   |  const int width = s->planewidth[plane];
508   |  const int pgroup_size = s->pgroup_size;
509   |  const int group_size = s->group_size;
510   |  const float sigma_sqr = s->sigma * s->sigma;
511   |  float *buffer = sc->buffer;
512   |  float *bufferh = sc->bufferh;
513   |  float *bufferv = sc->bufferv;
514   |  float *bufferz = sc->bufferz;
515   |  float *rbuffer = sc->rbuffer;
516   |  float *rbufferh = sc->rbufferh;
517   |  float *rbufferv = sc->rbufferv;
518   |  float *rbufferz = sc->rbufferz;
519   |  float den_weight, num_weight;
520   |  float l2_wiener = 0;
521   |  
522   |  for (int k = 0; k < nb_match_blocks; k++) {
    1Assuming 'k' is >= 'nb_match_blocks'→
    2←Loop condition is false. Execution continues on line 550→
523   |  const int y = sc->match_blocks[k].y;
524   |  const int x = sc->match_blocks[k].x;
525   |  
526   |  for (int i = 0; i < block_size; i++) {
527   |             s->get_block_row(src, src_linesize, y + i, x, block_size, bufferh + pblock_size * i);
528   |             s->get_block_row(ref, ref_linesize, y + i, x, block_size, rbufferh + pblock_size * i);
529   |             sc->tx_fn(sc->dctf, bufferh + pblock_size * i, bufferh + pblock_size * i, sizeof(float));
530   |             sc->tx_fn(sc->dctf, rbufferh + pblock_size * i, rbufferh + pblock_size * i, sizeof(float));
531   |         }
532   |  
533   |  for (int i = 0; i < block_size; i++) {
534   |  for (int j = 0; j < block_size; j++) {
535   |                 bufferv[i * pblock_size + j] = bufferh[j * pblock_size + i];
536   |                 rbufferv[i * pblock_size + j] = rbufferh[j * pblock_size + i];
537   |             }
538   |             sc->tx_fn(sc->dctf, bufferv + i * pblock_size, bufferv + i * pblock_size, sizeof(float));
539   |             sc->tx_fn(sc->dctf, rbufferv + i * pblock_size, rbufferv + i * pblock_size, sizeof(float));
540   |         }
541   |  
542   |  for (int i = 0; i < block_size; i++) {
543   |             memcpy(buffer + k * buffer_linesize + i * pblock_size,
544   |                    bufferv + i * pblock_size, block_size * sizeof(float));
545   |             memcpy(rbuffer + k * buffer_linesize + i * pblock_size,
546   |                    rbufferv + i * pblock_size, block_size * sizeof(float));
547   |         }
548   |     }
549   |  
550   |  for (int i = 0; i < block_size; i++) {
    3←Assuming 'i' is >= 'block_size'→
    4←Loop condition is false. Execution continues on line 565→
551   |  for (int j = 0; j < block_size; j++) {
552   |  for (int k = 0; k < nb_match_blocks; k++) {
553   |                 bufferz[k] = buffer[buffer_linesize * k + i * pblock_size + j];
554   |                 rbufferz[k] = rbuffer[buffer_linesize * k + i * pblock_size + j];
555   |             }
556   |  if (group_size > 1) {
557   |                 sc->tx_fn_g(sc->gdctf, bufferz, bufferz, sizeof(float));
558   |                 sc->tx_fn_g(sc->gdctf, rbufferz, rbufferz, sizeof(float));
559   |             }
560   |             bufferz += pgroup_size;
561   |             rbufferz += pgroup_size;
562   |         }
563   |     }
564   |  
565   |  bufferz = sc->bufferz;
566   |     rbufferz = sc->rbufferz;
567   |  
568   |  for (int i = 0; i4.1'i' is >= 'block_size' < block_size; i++) {
    5←Loop condition is false. Execution continues on line 584→
569   |  for (int j = 0; j < block_size; j++) {
570   |  for (int k = 0; k < nb_match_blocks; k++) {
571   |  const float ref_sqr = rbufferz[k] * rbufferz[k];
572   |  float wiener_coef = ref_sqr / (ref_sqr + sigma_sqr);
573   |  
574   |  if (isnan(wiener_coef))
575   |                    wiener_coef = 1;
576   |                 bufferz[k] *= wiener_coef;
577   |                 l2_wiener += wiener_coef * wiener_coef;
578   |             }
579   |             bufferz += pgroup_size;
580   |             rbufferz += pgroup_size;
581   |         }
582   |     }
583   |  
584   |  bufferz = sc->bufferz;
585   |     buffer = sc->buffer;
586   |  for (int i = 0; i5.1'i' is >= 'block_size' < block_size; i++) {
587   |  for (int j = 0; j < block_size; j++) {
588   |  if (group_size > 1)
589   |                 sc->itx_fn_g(sc->gdcti, bufferz, bufferz, sizeof(float));
590   |  for (int k = 0; k < nb_match_blocks; k++) {
591   |                 buffer[buffer_linesize * k + i * pblock_size + j] = bufferz[k];
592   |             }
593   |             bufferz += pgroup_size;
594   |         }
595   |     }
596   |  
597   |  l2_wiener = FFMAX(l2_wiener, 1e-15f);
    6←Loop condition is false. Execution continues on line 597→
    7←Assuming the condition is false→
    8←'?' condition is false→
598   |     den_weight = 1.f / l2_wiener;
    9←division by possibly zero aggregate factor
599   |     num_weight = den_weight;
600   |  
601   |  for (int k = 0; k < nb_match_blocks; k++) {
602   |  float *num = sc->num + y * width + x;
603   |  float *den = sc->den + y * width + x;
604   |  
605   |  for (int i = 0; i < block_size; i++) {
606   |             memcpy(bufferv + i * pblock_size,
607   |                    buffer + k * buffer_linesize + i * pblock_size,
608   |                    block_size * sizeof(float));
609   |         }
610   |  
611   |  for (int i = 0; i < block_size; i++) {
612   |             sc->itx_fn(sc->dcti, bufferv + pblock_size * i, bufferv + pblock_size * i, sizeof(float));
613   |  for (int j = 0; j < block_size; j++) {
614   |                 bufferh[j * pblock_size + i] = bufferv[i * pblock_size + j];
615   |             }
616   |         }
617   |  
618   |  for (int i = 0; i < block_size; i++) {
619   |             sc->itx_fn(sc->dcti, bufferh + pblock_size * i, bufferh + pblock_size * i, sizeof(float));
620   |  for (int j = 0; j < block_size; j++) {
621   |                 num[j] += bufferh[i * pblock_size + j] * num_weight;
622   |                 den[j] += den_weight;
623   |             }
624   |             num += width;
625   |             den += width;
626   |         }
627   |     }
628   | }