### Report Summary

File:| format/flvdec.c  
---|---  
Warning:| line 1359, column 28  
buffer read by avio_read may be partially uninitialized  
  
### Annotated Source Code


1267  |  if (!metadata)
1268  |  return AVERROR(ENOMEM);
1269  |  if (!av_packet_side_data_add(&st->codecpar->coded_side_data, &st->codecpar->nb_coded_side_data,
1270  |                                         AV_PKT_DATA_CONTENT_LIGHT_LEVEL, metadata, size, 0)) {
1271  |             av_freep(&metadata);
1272  |  return AVERROR(ENOMEM);
1273  |         }
1274  |         metadata->MaxCLL  = meta_video_color->max_cll;
1275  |         metadata->MaxFALL = meta_video_color->max_fall;
1276  |     }
1277  |  
1278  |  if (has_mastering_primaries || has_mastering_luminance) {
1279  |         size_t size = 0;
1280  |         AVMasteringDisplayMetadata *metadata = av_mastering_display_metadata_alloc_size(&size);
1281  |         AVPacketSideData *sd;
1282  |  
1283  |  if (!metadata)
1284  |  return AVERROR(ENOMEM);
1285  |  
1286  |         sd = av_packet_side_data_add(&st->codecpar->coded_side_data,
1287  |                                                         &st->codecpar->nb_coded_side_data,
1288  |                                                         AV_PKT_DATA_MASTERING_DISPLAY_METADATA,
1289  |                                                         metadata, size, 0);
1290  |  if (!sd) {
1291  |             av_freep(&metadata);
1292  |  return AVERROR(ENOMEM);
1293  |         }
1294  |  
1295  |  // hdrCll
1296  |  if (has_mastering_luminance) {
1297  |             metadata->max_luminance = av_d2q(mastering_meta->max_luminance, INT_MAX);
1298  |             metadata->min_luminance = av_d2q(mastering_meta->min_luminance, INT_MAX);
1299  |             metadata->has_luminance = 1;
1300  |         }
1301  |  // hdrMdcv
1302  |  if (has_mastering_primaries) {
1303  |             metadata->display_primaries[0][0] = av_d2q(mastering_meta->r_x, INT_MAX);
1304  |             metadata->display_primaries[0][1] = av_d2q(mastering_meta->r_y, INT_MAX);
1305  |             metadata->display_primaries[1][0] = av_d2q(mastering_meta->g_x, INT_MAX);
1306  |             metadata->display_primaries[1][1] = av_d2q(mastering_meta->g_y, INT_MAX);
1307  |             metadata->display_primaries[2][0] = av_d2q(mastering_meta->b_x, INT_MAX);
1308  |             metadata->display_primaries[2][1] = av_d2q(mastering_meta->b_y, INT_MAX);
1309  |             metadata->white_point[0] = av_d2q(mastering_meta->white_x, INT_MAX);
1310  |             metadata->white_point[1] = av_d2q(mastering_meta->white_y, INT_MAX);
1311  |             metadata->has_primaries = 1;
1312  |         }
1313  |     }
1314  |  return 0;
1315  | }
1316  |  
1317  | static int flv_parse_mod_ex_data(AVFormatContext *s, int *pkt_type, int *size, int64_t *dts)
1318  | {
1319  |  int ex_type, ret;
1320  |     uint8_t *ex_data;
1321  |  
1322  |  int ex_size = (uint8_t)avio_r8(s->pb) + 1;
1323  |     *size -= 1;
1324  |  
1325  |  if (ex_size == 256) {
    1Assuming 'ex_size' is not equal to 256→
    2←Taking false branch→
1326  |         ex_size = (uint16_t)avio_rb16(s->pb) + 1;
1327  |         *size -= 2;
1328  |     }
1329  |  
1330  |  if (ex_size >= *size) {
    3←Assuming the condition is false→
    4←Taking false branch→
1331  |         av_log(s, AV_LOG_WARNING, "ModEx size larger than remaining data!\n");
1332  |  return AVERROR(EINVAL);
1333  |     }
1334  |  
1335  |  ex_data = av_malloc(ex_size);
1336  |  if (!ex_data)
    5←Assuming 'ex_data' is non-null→
    6←Taking false branch→
1337  |  return AVERROR(ENOMEM);
1338  |  
1339  |  ret = avio_read(s->pb, ex_data, ex_size);
1340  |  if (ret < 0) {
    7←Assuming 'ret' is >= 0→
    8←Taking false branch→
1341  |         av_free(ex_data);
1342  |  return ret;
1343  |     }
1344  |  *size -= ex_size;
1345  |  
1346  |     ex_type = (uint8_t)avio_r8(s->pb);
1347  |     *size -= 1;
1348  |  
1349  |     *pkt_type = ex_type & 0x0f;
1350  |     ex_type &= 0xf0;
1351  |  
1352  |  if (ex_type == PacketModExTypeTimestampOffsetNano) {
    9←Assuming 'ex_type' is equal to PacketModExTypeTimestampOffsetNano→
    10←Taking true branch→
1353  |  uint32_t nano_offset;
1354  |  
1355  |  if (ex_size != 3) {
    11←Assuming 'ex_size' is equal to 3→
    12←Taking false branch→
1356  |             av_log(s, AV_LOG_WARNING, "Invalid ModEx size for Type TimestampOffsetNano!\n");
1357  |             nano_offset = 0;
1358  |         } else {
1359  |  nano_offset = (ex_data[0] << 16) | (ex_data[1] << 8) | ex_data[2];
    13←buffer read by avio_read may be partially uninitialized
1360  |         }
1361  |  
1362  |  // this is not likely to ever add anything, but right now timestamps are with ms precision
1363  |         *dts += nano_offset / 1000000;
1364  |     } else {
1365  |         av_log(s, AV_LOG_INFO, "Unknown ModEx type: %d", ex_type);
1366  |     }
1367  |  
1368  |     av_free(ex_data);
1369  |  
1370  |  return 0;
1371  | }
1372  |  
1373  | static int flv_read_packet(AVFormatContext *s, AVPacket *pkt)
1374  | {
1375  |     FLVContext *flv = s->priv_data;
1376  |  int ret = AVERROR_BUG, i, size, flags;
1377  |  int res = 0;
1378  |  enum FlvTagType type;
1379  |  int stream_type = -1;
1380  |     int64_t next, pos, meta_pos;
1381  |     int64_t dts, pts = AV_NOPTS_VALUE;
1382  |  int av_uninit(channels);
1383  |  int av_uninit(sample_rate);
1384  |     AVStream *st = NULL;
1385  |  int last = -1;
1386  |  int orig_size;
1387  |  int enhanced_flv = 0;
1388  |  int multitrack = 0;
1389  |  int pkt_type = 0;