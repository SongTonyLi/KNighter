### Report Summary

File:| /home/cc/ai2sec/KNighter/ffmpeg/libavformat/mxfdec.c  
---|---  
Warning:| line 3410, column 30  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


372   |  case TaggedValue:
373   |         av_freep(&((MXFTaggedValue *)*ctx)->name);
374   |         av_freep(&((MXFTaggedValue *)*ctx)->value);
375   |  break;
376   |  case Track:
377   |         av_freep(&((MXFTrack *)*ctx)->name);
378   |  break;
379   |  case IndexTableSegment:
380   |         seg = (MXFIndexTableSegment *)*ctx;
381   |         av_freep(&seg->temporal_offset_entries);
382   |         av_freep(&seg->flag_entries);
383   |         av_freep(&seg->stream_offset_entries);
384   |  default:
385   |  break;
386   |     }
387   |  if (freectx) {
388   |         av_freep(ctx);
389   |     }
390   | }
391   |  
392   | static int64_t klv_decode_ber_length(AVIOContext *pb)
393   | {
394   |     uint64_t size = avio_r8(pb);
395   |  if (size & 0x80) { /* long form */
396   |  int bytes_num = size & 0x7f;
397   |  /* SMPTE 379M 5.3.4 guarantee that bytes_num must not exceed 8 bytes */
398   |  if (bytes_num > 8)
399   |  return AVERROR_INVALIDDATA;
400   |         size = 0;
401   |  while (bytes_num--)
402   |             size = size << 8 | avio_r8(pb);
403   |     }
404   |  if (size > INT64_MAX)
405   |  return AVERROR_INVALIDDATA;
406   |  return size;
407   | }
408   |  
409   | static int mxf_read_sync(AVIOContext *pb, const uint8_t *key, unsigned size)
410   | {
411   |  int i, b;
412   |  for (i = 0; i < size && !avio_feof(pb); i++) {
413   |         b = avio_r8(pb);
414   |  if (b == key[0])
415   |             i = 0;
416   |  else if (b != key[i])
417   |             i = -1;
418   |     }
419   |  return i == size;
420   | }
421   |  
422   | static int klv_read_packet(KLVPacket *klv, AVIOContext *pb)
423   | {
424   |     int64_t length, pos;
425   |  if (!mxf_read_sync(pb, mxf_klv_key, 4))
426   |  return AVERROR_INVALIDDATA;
427   |     klv->offset = avio_tell(pb) - 4;
428   |     memcpy(klv->key, mxf_klv_key, 4);
429   |     avio_read(pb, klv->key + 4, 12);
430   |     length = klv_decode_ber_length(pb);
431   |  if (length < 0)
432   |  return length;
433   |     klv->length = length;
434   |     pos = avio_tell(pb);
435   |  if (pos > INT64_MAX - length)
436   |  return AVERROR_INVALIDDATA;
437   |     klv->next_klv = pos + length;
438   |  return 0;
439   | }
440   |  
441   | static int mxf_get_stream_index(AVFormatContext *s, KLVPacket *klv, int body_sid)
442   | {
443   |  int i;
444   |  
445   |  for (i = 0; i < s->nb_streams; i++) {
446   |         MXFTrack *track = s->streams[i]->priv_data;
447   |  /* SMPTE 379M 7.3 */
448   |  if (track && (!body_sid || !track->body_sid || track->body_sid == body_sid) && !memcmp(klv->key + sizeof(mxf_essence_element_key), track->track_number, sizeof(track->track_number)))
449   |  return i;
450   |     }
451   |  /* return 0 if only one stream, for OP Atom files with 0 as track number */
452   |  return s->nb_streams == 1 && s->streams[0]->priv_data ? 0 : -1;
453   | }
454   |  
455   | static int find_body_sid_by_absolute_offset(MXFContext *mxf, int64_t offset)
456   | {
457   |  // we look for partition where the offset is placed
458   |  int a, b, m;
459   |     int64_t pack_ofs;
460   |  
461   |     a = -1;
462   |     b = mxf->partitions_count;
463   |  
464   |  while (b - a > 1) {
465   |         m = (a + b) >> 1;
466   |         pack_ofs = mxf->partitions[m].pack_ofs;
467   |  if (pack_ofs <= offset)
468   |             a = m;
2919  |  
2920  |         av_log(mxf->fc, AV_LOG_TRACE, "local tag %#04x size %d\n", tag, size);
2921  |  if (!size) { /* ignore empty tag, needed for some files with empty UMID tag */
2922  |             av_log(mxf->fc, AV_LOG_ERROR, "local tag %#04x with 0 size\n", tag);
2923  |  continue;
2924  |         }
2925  |  if (tag > 0x7FFF) { /* dynamic tag */
2926  |  int i;
2927  |  for (i = 0; i < mxf->local_tags_count; i++) {
2928  |  int local_tag = AV_RB16(mxf->local_tags+i*18);
2929  |  if (local_tag == tag) {
2930  |                     memcpy(uid, mxf->local_tags+i*18+2, 16);
2931  |                     av_log(mxf->fc, AV_LOG_TRACE, "local tag %#04x\n", local_tag);
2932  |  PRINT_KEY(mxf->fc, "uid", uid);
2933  |                 }
2934  |             }
2935  |         }
2936  |  if (meta && tag == 0x3C0A) {
2937  |             avio_read(pb, meta->uid, 16);
2938  |         } else if ((ret = read_child(ctx, pb, tag, size, uid, -1)) < 0) {
2939  |  if (meta) {
2940  |                 mxf_free_metadataset(&meta, 1);
2941  |             }
2942  |  return ret;
2943  |         }
2944  |  
2945  |  /* Accept the 64k local set limit being exceeded (Avid). Don't accept
2946  |  * it extending past the end of the KLV though (zzuf5.mxf). */
2947  |  if (avio_tell(pb) > klv_end) {
2948  |  if (meta) {
2949  |                 mxf_free_metadataset(&meta, 1);
2950  |             }
2951  |  
2952  |             av_log(mxf->fc, AV_LOG_ERROR,
2953  |  "local tag %#04x extends past end of local set @ %#"PRIx64"\n",
2954  |                    tag, klv->offset);
2955  |  return AVERROR_INVALIDDATA;
2956  |         } else if (avio_tell(pb) <= next)   /* only seek forward, else this can loop for a long time */
2957  |             avio_seek(pb, next, SEEK_SET);
2958  |     }
2959  |  return meta ? mxf_add_metadata_set(mxf, &meta) : 0;
2960  | }
2961  |  
2962  | /**
2963  |  * Matches any partition pack key, in other words:
2964  |  * - HeaderPartition
2965  |  * - BodyPartition
2966  |  * - FooterPartition
2967  |  * @return non-zero if the key is a partition pack key, zero otherwise
2968  |  */
2969  | static int mxf_is_partition_pack_key(UID key)
2970  | {
2971  |  //NOTE: this is a little lax since it doesn't constraint key[14]
2972  |  return !memcmp(key, mxf_header_partition_pack_key, 13) &&
2973  |             key[13] >= 2 && key[13] <= 4;
2974  | }
2975  |  
2976  | /**
2977  |  * Parses a metadata KLV
2978  |  * @return <0 on error, 0 otherwise
2979  |  */
2980  | static int mxf_parse_klv(MXFContext *mxf, KLVPacket klv, MXFMetadataReadFunc *read,
2981  |  int ctx_size, enum MXFMetadataSetType type)
2982  | {
2983  |     AVFormatContext *s = mxf->fc;
2984  |  int res;
2985  |  if (klv.key[5] == 0x53) {
2986  |         res = mxf_read_local_tags(mxf, &klv, read, ctx_size, type);
2987  |     } else {
2988  |         uint64_t next = avio_tell(s->pb) + klv.length;
2989  |         res = read(mxf, s->pb, 0, klv.length, klv.key, klv.offset);
2990  |  
2991  |  /* only seek forward, else this can loop for a long time */
2992  |  if (avio_tell(s->pb) > next) {
2993  |             av_log(s, AV_LOG_ERROR, "read past end of KLV @ %#"PRIx64"\n",
2994  |                    klv.offset);
2995  |  return AVERROR_INVALIDDATA;
2996  |         }
2997  |  
2998  |         avio_seek(s->pb, next, SEEK_SET);
2999  |     }
3000  |  if (res < 0) {
3001  |         av_log(s, AV_LOG_ERROR, "error reading header metadata\n");
3002  |  return res;
3231  |         }
3232  |     }
3233  |  
3234  |  /* find the essence partition */
3235  |  for (i = 0; i < mxf->partitions_count; i++) {
3236  |  /* BodySID == 0 -> no essence */
3237  |  if (mxf->partitions[i].body_sid != track->body_sid)
3238  |  continue;
3239  |  
3240  |         p = &mxf->partitions[i];
3241  |         essence_partition_count++;
3242  |     }
3243  |  
3244  |  /* only handle files with a single essence partition */
3245  |  if (essence_partition_count != 1)
3246  |  return 0;
3247  |  
3248  |  if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && is_pcm(st->codecpar->codec_id)) {
3249  |         edit_unit_byte_count = (av_get_bits_per_sample(st->codecpar->codec_id) * st->codecpar->channels) >> 3;
3250  |     } else if (st->duration > 0 && p->first_essence_klv.length > 0 && p->first_essence_klv.length % st->duration == 0) {
3251  |         edit_unit_byte_count = p->first_essence_klv.length / st->duration;
3252  |     }
3253  |  
3254  |  if (edit_unit_byte_count <= 0)
3255  |  return 0;
3256  |  
3257  |     av_log(mxf->fc, AV_LOG_WARNING, "guessing index for stream %d using edit unit byte count %d\n", st->index, edit_unit_byte_count);
3258  |  
3259  |  if (!(segment = av_mallocz(sizeof(*segment))))
3260  |  return AVERROR(ENOMEM);
3261  |  
3262  |  if ((ret = mxf_add_metadata_set(mxf, (MXFMetadataSet**)&segment)))
3263  |  return ret;
3264  |  
3265  |  /* Make sure we have nonzero unique index_sid, body_sid will be ok, because
3266  |  * using the same SID for index is forbidden in MXF. */
3267  |  if (!track->index_sid)
3268  |         track->index_sid = track->body_sid;
3269  |  
3270  |     segment->type = IndexTableSegment;
3271  |  /* stream will be treated as small EditUnitByteCount */
3272  |     segment->edit_unit_byte_count = edit_unit_byte_count;
3273  |     segment->index_start_position = 0;
3274  |     segment->index_duration = st->duration;
3275  |     segment->index_edit_rate = av_inv_q(st->time_base);
3276  |     segment->index_sid = track->index_sid;
3277  |     segment->body_sid = p->body_sid;
3278  |  return 0;
3279  | }
3280  |  
3281  | static void mxf_read_random_index_pack(AVFormatContext *s)
3282  | {
3283  |     MXFContext *mxf = s->priv_data;
3284  |     uint32_t length;
3285  |     int64_t file_size, max_rip_length, min_rip_length;
3286  |     KLVPacket klv;
3287  |  
3288  |  if (!(s->pb->seekable & AVIO_SEEKABLE_NORMAL))
3289  |  return;
3290  |  
3291  |     file_size = avio_size(s->pb);
3292  |  
3293  |  /* S377m says to check the RIP length for "silly" values, without defining "silly".
3294  |  * The limit below assumes a file with nothing but partition packs and a RIP.
3295  |  * Before changing this, consider that a muxer may place each sample in its own partition.
3296  |  *
3297  |  * 105 is the size of the smallest possible PartitionPack
3298  |  * 12 is the size of each RIP entry
3299  |  * 28 is the size of the RIP header and footer, assuming an 8-byte BER
3300  |  */
3301  |     max_rip_length = ((file_size - mxf->run_in) / 105) * 12 + 28;
3302  |     max_rip_length = FFMIN(max_rip_length, INT_MAX); //2 GiB and up is also silly
3303  |  
3304  |  /* We're only interested in RIPs with at least two entries.. */
3305  |     min_rip_length = 16+1+24+4;
3306  |  
3307  |  /* See S377m section 11 */
3308  |     avio_seek(s->pb, file_size - 4, SEEK_SET);
3309  |     length = avio_rb32(s->pb);
3310  |  
3311  |  if (length < min_rip_length || length > max_rip_length)
3312  |  goto end;
3313  |     avio_seek(s->pb, file_size - length, SEEK_SET);
3314  |  if (klv_read_packet(&klv, s->pb) < 0 ||
3315  |         !IS_KLV_KEY(klv.key, ff_mxf_random_index_pack_key))
3316  |  goto end;
3317  |  if (klv.next_klv != file_size || klv.length <= 4 || (klv.length - 4) % 12) {
3318  |         av_log(s, AV_LOG_WARNING, "Invalid RIP KLV length\n");
3319  |  goto end;
3320  |     }
3321  |  
3322  |     avio_skip(s->pb, klv.length - 12);
3323  |     mxf->footer_partition = avio_rb64(s->pb);
3324  |  
3325  |  /* sanity check */
3326  |  if (mxf->run_in + mxf->footer_partition >= file_size) {
3327  |         av_log(s, AV_LOG_WARNING, "bad FooterPartition in RIP - ignoring\n");
3328  |         mxf->footer_partition = 0;
3329  |     }
3330  |  
3331  | end:
3332  |     avio_seek(s->pb, mxf->run_in, SEEK_SET);
3333  | }
3334  |  
3335  | static int mxf_read_header(AVFormatContext *s)
3336  | {
3337  |  MXFContext *mxf = s->priv_data;
3338  |     KLVPacket klv;
3339  |     int64_t essence_offset = 0;
3340  |  int ret;
3341  |  
3342  |     mxf->last_forward_tell = INT64_MAX;
3343  |  
3344  |  if (!mxf_read_sync(s->pb, mxf_header_partition_pack_key, 14)) {
    1Assuming the condition is false→
    2←Taking false branch→
3345  |         av_log(s, AV_LOG_ERROR, "could not find header partition pack key\n");
3346  |  //goto fail should not be needed as no metadata sets will have been parsed yet
3347  |  return AVERROR_INVALIDDATA;
3348  |     }
3349  |  avio_seek(s->pb, -14, SEEK_CUR);
3350  |     mxf->fc = s;
3351  |     mxf->run_in = avio_tell(s->pb);
3352  |  
3353  |     mxf_read_random_index_pack(s);
3354  |  
3355  |  while (!avio_feof(s->pb)) {
    3←Assuming the condition is true→
    4←Loop condition is true.  Entering loop body→
3356  |  const MXFMetadataReadTableEntry *metadata;
3357  |  
3358  |  if (klv_read_packet(&klv, s->pb) < 0) {
    5←Taking false branch→
3359  |  /* EOF - seek to previous partition or stop */
3360  |  if(mxf_parse_handle_partition_or_eof(mxf) <= 0)
3361  |  break;
3362  |  else
3363  |  continue;
3364  |         }
3365  |  
3366  |  PRINT_KEY(s, "read header", klv.key);
    6←Taking false branch→
    7←Loop condition is false.  Exiting loop→
3367  |  av_log(s, AV_LOG_TRACE, "size %"PRIu64" offset %#"PRIx64"\n", klv.length, klv.offset);
3368  |  if (IS_KLV_KEY(klv.key, mxf_encrypted_triplet_key) ||
    8←Assuming the condition is false→
3369  |  IS_KLV_KEY(klv.key, mxf_essence_element_key) ||
    9←Assuming the condition is false→
3370  |  IS_KLV_KEY(klv.key, mxf_canopus_essence_element_key) ||
    10←Assuming the condition is false→
3371  |  IS_KLV_KEY(klv.key, mxf_avid_essence_element_key) ||
    11←Assuming the condition is false→
3372  |  IS_KLV_KEY(klv.key, mxf_system_item_key_cp) ||
    12←Assuming the condition is false→
3373  |  IS_KLV_KEY(klv.key, mxf_system_item_key_gc)) {
    13←Assuming the condition is false→
3374  |  
3375  |  if (!mxf->current_partition) {
3376  |                 av_log(mxf->fc, AV_LOG_ERROR, "found essence prior to first PartitionPack\n");
3377  |                 ret = AVERROR_INVALIDDATA;
3378  |  goto fail;
3379  |             }
3380  |  
3381  |  if (!mxf->current_partition->first_essence_klv.offset)
3382  |                 mxf->current_partition->first_essence_klv = klv;
3383  |  
3384  |  if (!essence_offset)
3385  |                 essence_offset = klv.offset;
3386  |  
3387  |  /* seek to footer, previous partition or stop */
3388  |  if (mxf_parse_handle_essence(mxf) <= 0)
3389  |  break;
3390  |  continue;
3391  |         } else if (mxf_is_partition_pack_key(klv.key) && mxf->current_partition) {
3392  |  /* next partition pack - keep going, seek to previous partition or stop */
3393  |  if(mxf_parse_handle_partition_or_eof(mxf) <= 0)
3394  |  break;
3395  |  else if (mxf->parsing_backward)
3396  |  continue;
3397  |  /* we're still parsing forward. proceed to parsing this partition pack */
3398  |         }
3399  |  
3400  |  for (metadata = mxf_metadata_read_table; metadata->read; metadata++) {
    14←Loop condition is false. Execution continues on line 3407→
3401  |  if (IS_KLV_KEY(klv.key, metadata->key)) {
3402  |  if ((ret = mxf_parse_klv(mxf, klv, metadata->read, metadata->ctx_size, metadata->type)) < 0)
3403  |  goto fail;
3404  |  break;
3405  |             }
3406  |         }
3407  |  if (!metadata->read14.1Field 'read' is null) {
    15←Taking true branch→
3408  |  av_log(s, AV_LOG_VERBOSE, "Dark key " PRIxUID "\n",
3409  |  UID_ARG(klv.key));
3410  |  avio_skip(s->pb, klv.length);
    16←buffer read by avio_read may be partially uninitialized
3411  |         }
3412  |     }
3413  |  /* FIXME avoid seek */
3414  |  if (!essence_offset)  {
3415  |         av_log(s, AV_LOG_ERROR, "no essence\n");
3416  |         ret = AVERROR_INVALIDDATA;
3417  |  goto fail;
3418  |     }
3419  |     avio_seek(s->pb, essence_offset, SEEK_SET);
3420  |  
3421  |  /* we need to do this before computing the index tables
3422  |  * to be able to fill in zero IndexDurations with st->duration */
3423  |  if ((ret = mxf_parse_structural_metadata(mxf)) < 0)
3424  |  goto fail;
3425  |  
3426  |  for (int i = 0; i < s->nb_streams; i++)
3427  |         mxf_handle_missing_index_segment(mxf, s->streams[i]);
3428  |  
3429  |  if ((ret = mxf_compute_index_tables(mxf)) < 0)
3430  |  goto fail;
3431  |  
3432  |  if (mxf->nb_index_tables > 1) {
3433  |  /* TODO: look up which IndexSID to use via EssenceContainerData */
3434  |         av_log(mxf->fc, AV_LOG_INFO, "got %i index tables - only the first one (IndexSID %i) will be used\n",
3435  |                mxf->nb_index_tables, mxf->index_tables[0].index_sid);
3436  |     } else if (mxf->nb_index_tables == 0 && mxf->op == OPAtom && (s->error_recognition & AV_EF_EXPLODE)) {
3437  |         av_log(mxf->fc, AV_LOG_ERROR, "cannot demux OPAtom without an index\n");
3438  |         ret = AVERROR_INVALIDDATA;
3439  |  goto fail;
3440  |     }