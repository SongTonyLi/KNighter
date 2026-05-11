# Instruction

Determine whether the static analyzer report is a real bug in the Linux kernel and matches the target bug pattern

Your analysis should:
- **Compare the report against the provided target bug pattern specification,** using the **buggy function (pre-patch)** and the **fix patch** as the reference.
- Explain your reasoning for classifying this as either:
  - **A true positive** (matches the target bug pattern **and** is a real bug), or
  - **A false positive** (does **not** match the target bug pattern **or** is **not** a real bug).

Please evaluate thoroughly using the following process:

- **First, understand** the reported code pattern and its control/data flow.
- **Then, compare** it against the target bug pattern characteristics.
- **Finally, validate** against the **pre-/post-patch** behavior:
  - The reported case demonstrates the same root cause pattern as the target bug pattern/function and would be addressed by a similar fix.

- **Numeric / bounds feasibility** (if applicable):
  - Infer tight **min/max** ranges for all involved variables from types, prior checks, and loop bounds.
  - Show whether overflow/underflow or OOB is actually triggerable (compute the smallest/largest values that violate constraints).

- **Null-pointer dereference feasibility** (if applicable):
  1. **Identify the pointer source** and return convention of the producing function(s) in this path (e.g., returns **NULL**, **ERR_PTR**, negative error code via cast, or never-null).
  2. **Check real-world feasibility in this specific driver/socket/filesystem/etc.**:
     - Enumerate concrete conditions under which the producer can return **NULL/ERR_PTR** here (e.g., missing DT/ACPI property, absent PCI device/function, probe ordering, hotplug/race, Kconfig options, chip revision/quirks).
     - Verify whether those conditions can occur given the driver’s init/probe sequence and the kernel helpers used.
  3. **Lifetime & concurrency**: consider teardown paths, RCU usage, refcounting (`get/put`), and whether the pointer can become invalid/NULL across yields or callbacks.
  4. If the producer is provably non-NULL in this context (by spec or preceding checks), classify as **false positive**.

If there is any uncertainty in the classification, **err on the side of caution and classify it as a false positive**. Your analysis will be used to improve the static analyzer's accuracy.

## Bug Pattern

The bug pattern is **performing floating-point division using an accumulated or computed denominator without checking whether it is zero**.

In this code, `norm_fac` is built by summing per-band contributions:

```c
norm_fac += band->norm_fac;
```

and is later used as a divisor:

```c
norm_fac = 1.0f / norm_fac;
```

If all contributions are zero, `norm_fac` remains `0.0f`, causing a divide-by-zero. This commonly happens when a normalization/scaling factor is derived from runtime data and the code assumes it must be nonzero, but valid inputs can make the sum/product remain zero.

So the specific bug pattern is:

- compute an aggregate normalization factor from data-dependent values,
- later invert or divide by it,
- **without guarding against the aggregate being zero**.

## Bug Pattern

The bug pattern is **performing floating-point division using an accumulated or computed denominator without checking whether it is zero**.

In this code, `norm_fac` is built by summing per-band contributions:

```c
norm_fac += band->norm_fac;
```

and is later used as a divisor:

```c
norm_fac = 1.0f / norm_fac;
```

If all contributions are zero, `norm_fac` remains `0.0f`, causing a divide-by-zero. This commonly happens when a normalization/scaling factor is derived from runtime data and the code assumes it must be nonzero, but valid inputs can make the sum/product remain zero.

So the specific bug pattern is:

- compute an aggregate normalization factor from data-dependent values,
- later invert or divide by it,
- **without guarding against the aggregate being zero**.

# Report

### Report Summary

File:| avcodec/qdm2.c  
---|---  
Warning:| line 833, column 66  
division by possibly zero aggregate factor  
  
### Annotated Source Code


613   |                         tmp = 0;
614   |  else if (comp <= 10)
615   |                         tmp = 10;
616   |  else if (comp <= 16)
617   |                         tmp = 16;
618   |  else if (comp <= 24)
619   |                         tmp = -1;
620   |  else
621   |                         tmp = 0;
622   |                     coding_method[ch][sb][j] = ((tmp & 0xfffa) + 30 )& 0xff;
623   |                 }
624   |  for (sb = 0; sb < 30; sb++)
625   |             fix_coding_method_array(sb, nb_channels, coding_method);
626   |  for (ch = 0; ch < nb_channels; ch++)
627   |  for (sb = 0; sb < 30; sb++)
628   |  for (j = 0; j < 64; j++)
629   |  if (sb >= 10) {
630   |  if (coding_method[ch][sb][j] < 10)
631   |                             coding_method[ch][sb][j] = 10;
632   |                     } else {
633   |  if (sb >= 2) {
634   |  if (coding_method[ch][sb][j] < 16)
635   |                                 coding_method[ch][sb][j] = 16;
636   |                         } else {
637   |  if (coding_method[ch][sb][j] < 30)
638   |                                 coding_method[ch][sb][j] = 30;
639   |                         }
640   |                     }
641   | #endif
642   |     } else { // superblocktype_2_3 != 0
643   |  for (ch = 0; ch < nb_channels; ch++)
644   |  for (sb = 0; sb < 30; sb++)
645   |  for (j = 0; j < 64; j++)
646   |                     coding_method[ch][sb][j] = coding_method_table[cm_table_select][sb];
647   |     }
648   |  return 0;
649   | }
650   |  
651   | /**
652   |  * Called by process_subpacket_11 to process more data from subpacket 11
653   |  * with sb 0-8.
654   |  * Called by process_subpacket_12 to process data from subpacket 12 with
655   |  * sb 8-sb_used.
656   |  *
657   |  * @param q         context
658   |  * @param gb        bitreader context
659   |  * @param length    packet length in bits
660   |  * @param sb_min    lower subband processed (sb_min included)
661   |  * @param sb_max    higher subband processed (sb_max excluded)
662   |  */
663   | static int synthfilt_build_sb_samples(QDM2Context *q, GetBitContext *gb,
664   |  int length, int sb_min, int sb_max)
665   | {
666   |  int sb, j, k, n, ch, run, channels;
667   |  int joined_stereo, zero_encoding;
668   |  int type34_first;
669   |  float type34_div = 0;
670   |  float type34_predictor;
671   |  float samples[10];
672   |  int sign_bits[16] = {0};
673   |  
674   |  if (length == 0) {
    1Assuming 'length' is not equal to 0→
    2←Taking false branch→
675   |  // If no data use noise
676   |  for (sb=sb_min; sb < sb_max; sb++) {
677   |  int ret = build_sb_samples_from_noise(q, sb);
678   |  if (ret < 0)
679   |  return ret;
680   |         }
681   |  
682   |  return 0;
683   |     }
684   |  
685   |  for (sb = sb_min; sb < sb_max; sb++) {
    3←Assuming 'sb' is < 'sb_max'→
    4←Loop condition is true.  Entering loop body→
686   |  channels = q->nb_channels;
687   |  
688   |  if (q->nb_channels <= 1 || sb < 12)
    5←Assuming field 'nb_channels' is > 1→
    6←Assuming 'sb' is < 12→
    7←Taking true branch→
689   |  joined_stereo = 0;
690   |  else if (sb >= 24)
691   |             joined_stereo = 1;
692   |  else
693   |             joined_stereo = (get_bits_left(gb) >= 1) ? get_bits1(gb) : 0;
694   |  
695   |  if (joined_stereo7.1'joined_stereo' is 0) {
    8←Taking false branch→
696   |  if (get_bits_left(gb) >= 16)
697   |  for (j = 0; j < 16; j++)
698   |                     sign_bits[j] = get_bits1(gb);
699   |  
700   |  for (j = 0; j < 64; j++)
701   |  if (q->coding_method[1][sb][j] > q->coding_method[0][sb][j])
702   |                     q->coding_method[0][sb][j] = q->coding_method[1][sb][j];
703   |  
704   |  if (fix_coding_method_array(sb, q->nb_channels,
705   |                                             q->coding_method)) {
706   |                 av_log(NULL, AV_LOG_ERROR, "coding method invalid\n");
707   |  int ret = build_sb_samples_from_noise(q, sb);
708   |  if (ret < 0)
709   |  return ret;
710   |  continue;
711   |             }
712   |             channels = 1;
713   |         }
714   |  
715   |  for (ch = 0; ch8.1'ch' is < 'channels' < channels; ch++) {
    9←Loop condition is true.  Entering loop body→
716   |  FIX_NOISE_IDX(q->noise_idx);
    10←Assuming field 'noise_idx' is < 3840→
717   |  zero_encoding = (get_bits_left(gb) >= 1) ? get_bits1(gb) : 0;
    11←Assuming the condition is true→
    12←'?' condition is true→
718   |             type34_predictor = 0.0;
719   |             type34_first = 1;
720   |  
721   |  for (j = 0; j < 128; ) {
    13←Loop condition is true.  Entering loop body→
    24←Loop condition is true.  Entering loop body→
722   |  switch (q->coding_method[ch][sb][j / 2]) {
    14←Control jumps to 'case 34:'  at line 820→
    25←Control jumps to 'case 34:'  at line 820→
723   |  case 8:
724   |  if (get_bits_left(gb) >= 10) {
725   |  if (zero_encoding) {
726   |  for (k = 0; k < 5; k++) {
727   |  if ((j + 2 * k) >= 128)
728   |  break;
729   |                                     samples[2 * k] = get_bits1(gb) ? dequant_1bit[joined_stereo][2 * get_bits1(gb)] : 0;
730   |                                 }
731   |                             } else {
732   |                                 n = get_bits(gb, 8);
733   |  if (n >= 243) {
734   |                                     av_log(NULL, AV_LOG_ERROR, "Invalid 8bit codeword\n");
735   |  return AVERROR_INVALIDDATA;
736   |                                 }
737   |  
738   |  for (k = 0; k < 5; k++)
739   |                                     samples[2 * k] = dequant_1bit[joined_stereo][random_dequant_index[n][k]];
740   |                             }
741   |  for (k = 0; k < 5; k++)
742   |                                 samples[2 * k + 1] = SB_DITHERING_NOISE(sb,q->noise_idx);
743   |                         } else {
744   |  for (k = 0; k < 10; k++)
745   |                                 samples[k] = SB_DITHERING_NOISE(sb,q->noise_idx);
746   |                         }
747   |                         run = 10;
748   |  break;
749   |  
750   |  case 10:
751   |  if (get_bits_left(gb) >= 1) {
752   |  float f = 0.81;
770   |                                     samples[k] = (get_bits1(gb) == 0) ? 0 : dequant_1bit[joined_stereo][2 * get_bits1(gb)];
771   |                                 }
772   |                             } else {
773   |                                 n = get_bits (gb, 8);
774   |  if (n >= 243) {
775   |                                     av_log(NULL, AV_LOG_ERROR, "Invalid 8bit codeword\n");
776   |  return AVERROR_INVALIDDATA;
777   |                                 }
778   |  
779   |  for (k = 0; k < 5; k++)
780   |                                     samples[k] = dequant_1bit[joined_stereo][random_dequant_index[n][k]];
781   |                             }
782   |                         } else {
783   |  for (k = 0; k < 5; k++)
784   |                                 samples[k] = SB_DITHERING_NOISE(sb,q->noise_idx);
785   |                         }
786   |                         run = 5;
787   |  break;
788   |  
789   |  case 24:
790   |  if (get_bits_left(gb) >= 7) {
791   |                             n = get_bits(gb, 7);
792   |  if (n >= 125) {
793   |                                 av_log(NULL, AV_LOG_ERROR, "Invalid 7bit codeword\n");
794   |  return AVERROR_INVALIDDATA;
795   |                             }
796   |  
797   |  for (k = 0; k < 3; k++)
798   |                                 samples[k] = (random_dequant_type24[n][k] - 2.0) * 0.5;
799   |                         } else {
800   |  for (k = 0; k < 3; k++)
801   |                                 samples[k] = SB_DITHERING_NOISE(sb,q->noise_idx);
802   |                         }
803   |                         run = 3;
804   |  break;
805   |  
806   |  case 30:
807   |  if (get_bits_left(gb) >= 4) {
808   |  unsigned index = qdm2_get_vlc(gb, &vlc_tab_type30, 0, 1);
809   |  if (index >= FF_ARRAY_ELEMS(type30_dequant)) {
810   |                                 av_log(NULL, AV_LOG_ERROR, "index %d out of type30_dequant array\n", index);
811   |  return AVERROR_INVALIDDATA;
812   |                             }
813   |                             samples[0] = type30_dequant[index];
814   |                         } else
815   |                             samples[0] = SB_DITHERING_NOISE(sb,q->noise_idx);
816   |  
817   |                         run = 1;
818   |  break;
819   |  
820   |  case 34:
821   |  if (get_bits_left(gb) >= 7) {
    15←Assuming the condition is true→
    16←Taking true branch→
    26←Assuming the condition is true→
    27←Taking true branch→
822   |  if (type34_first16.1'type34_first' is 127.1'type34_first' is 0) {
    17←Taking true branch→
    28←Taking false branch→
823   |  type34_div = (float)(1 << get_bits(gb, 2));
    18←Assuming right operand of bit shift is less than 32→
824   |                                 samples[0] = ((float)get_bits(gb, 5) - 16.0) / 15.0;
825   |                                 type34_predictor = samples[0];
826   |  type34_first = 0;
827   |                             } else {
828   |  unsigned index = qdm2_get_vlc(gb, &vlc_tab_type34, 0, 1);
829   |  if (index >= FF_ARRAY_ELEMS(type34_delta)) {
    29←Assuming the condition is false→
    30←Taking false branch→
830   |                                     av_log(NULL, AV_LOG_ERROR, "index %d out of type34_delta array\n", index);
831   |  return AVERROR_INVALIDDATA;
832   |                                 }
833   |  samples[0] = type34_delta[index] / type34_div + type34_predictor;
    31←division by possibly zero aggregate factor
834   |                                 type34_predictor = samples[0];
835   |                             }
836   |                         } else {
837   |                             samples[0] = SB_DITHERING_NOISE(sb,q->noise_idx);
838   |                         }
839   |  run = 1;
840   |  break;
841   |  
842   |  default:
843   |                         samples[0] = SB_DITHERING_NOISE(sb,q->noise_idx);
844   |                         run = 1;
845   |  break;
846   |                 }
847   |  
848   |  if (joined_stereo19.1'joined_stereo' is 0) {
    19← Execution continues on line 848→
    20←Taking false branch→
849   |  for (k = 0; k < run && j + k < 128; k++) {
850   |                         q->sb_samples[0][j + k][sb] =
851   |                             q->tone_level[0][sb][(j + k) / 2] * samples[k];
852   |  if (q->nb_channels == 2) {
853   |  if (sign_bits[(j + k) / 8])
854   |                                 q->sb_samples[1][j + k][sb] =
855   |                                     q->tone_level[1][sb][(j + k) / 2] * -samples[k];
856   |  else
857   |                                 q->sb_samples[1][j + k][sb] =
858   |                                     q->tone_level[1][sb][(j + k) / 2] * samples[k];
859   |                         }
860   |                     }
861   |                 } else {
862   |  for (k = 0; k < run; k++)
    21←Loop condition is true.  Entering loop body→
    23←Loop condition is false. Execution continues on line 867→
863   |  if ((j + k) < 128)
    22←Taking true branch→
864   |  q->sb_samples[ch][j + k][sb] = q->tone_level[ch][sb][(j + k)/2] * samples[k];
865   |                 }
866   |  
867   |  j += run;
868   |  } // j loop
869   |         } // channel loop
870   |     } // subband loop
871   |  return 0;
872   | }
873   |  
874   | /**
875   |  * Init the first element of a channel in quantized_coeffs with data
876   |  * from packet 10 (quantized_coeffs[ch][0]).
877   |  * This is similar to process_subpacket_9, but for a single channel
878   |  * and for element [0]
879   |  * same VLC tables as process_subpacket_9 are used.
880   |  *
881   |  * @param quantized_coeffs    pointer to quantized_coeffs[ch][0]
882   |  * @param gb        bitreader context
883   |  */
884   | static int init_quantized_coeffs_elem0(int8_t *quantized_coeffs,
885   |                                         GetBitContext *gb)
886   | {
887   |  int i, k, run, level, diff;
888   |  
889   |  if (get_bits_left(gb) < 16)
890   |  return AVERROR_INVALIDDATA;
891   |     level = qdm2_get_vlc(gb, &vlc_tab_level, 0, 2);
892   |  
893   |     quantized_coeffs[0] = level;
894   |  
895   |  for (i = 0; i < 7; ) {
896   |  if (get_bits_left(gb) < 16)
897   |  return AVERROR_INVALIDDATA;
898   |         run = qdm2_get_vlc(gb, &vlc_tab_run, 0, 1) + 1;

# Formatting

Please provide your answer in the following format:

- Decision: {Bug/NotABug}
- Reason: {Your reason here}
