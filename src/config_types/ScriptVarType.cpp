#include "config_types/ScriptVarType.h"

std::unordered_map<int, const ScriptVarType *> ScriptVarType::scriptVarTypes;
bool ScriptVarType::initialized = false;

void ScriptVarType::init()
{
    if (initialized)
        return;

    static const ScriptVarType INT(0, "INT", 'i', BaseVarType::INTEGER, 0);
    scriptVarTypes[INT.getId()] = &INT;

    static const ScriptVarType BOOLEAN(1, "BOOLEAN", '1', BaseVarType::INTEGER, 0);
    scriptVarTypes[BOOLEAN.getId()] = &BOOLEAN;

    static const ScriptVarType TYPE_2(2, '2', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_2.getId()] = &TYPE_2;

    static const ScriptVarType QUEST(3, "QUEST", ':', BaseVarType::INTEGER, -1);
    scriptVarTypes[QUEST.getId()] = &QUEST;

    static const ScriptVarType QUESTHELP(4, "QUESTHELP", ',', BaseVarType::INTEGER, -1);
    scriptVarTypes[QUESTHELP.getId()] = &QUESTHELP;

    static const ScriptVarType CURSOR(5, "CURSOR", '@', BaseVarType::INTEGER, -1);
    scriptVarTypes[CURSOR.getId()] = &CURSOR;

    static const ScriptVarType SEQ(6, "ANIMATION", 'A', BaseVarType::INTEGER, -1);
    scriptVarTypes[SEQ.getId()] = &SEQ;

    static const ScriptVarType COLOUR(7, "COLOUR", 'C', BaseVarType::INTEGER, -1);
    scriptVarTypes[COLOUR.getId()] = &COLOUR;

    static const ScriptVarType LOC_SHAPE(8, "LOC_SHAPE", 'H', BaseVarType::INTEGER, -1);
    scriptVarTypes[LOC_SHAPE.getId()] = &LOC_SHAPE;

    static const ScriptVarType COMPONENT(9, "COMPONENT", 'I', BaseVarType::INTEGER, -1);
    scriptVarTypes[COMPONENT.getId()] = &COMPONENT;

    static const ScriptVarType IDKIT(10, "IDKIT", 'K', BaseVarType::INTEGER, -1);
    scriptVarTypes[IDKIT.getId()] = &IDKIT;

    static const ScriptVarType MIDI(11, "MIDI", 'M', BaseVarType::INTEGER, -1);
    scriptVarTypes[MIDI.getId()] = &MIDI;

    static const ScriptVarType NPC_MODE(12, "NPC_MODE", 'N', BaseVarType::INTEGER, -1);
    scriptVarTypes[NPC_MODE.getId()] = &NPC_MODE;

    static const ScriptVarType TYPE_13(13, 'O', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_13.getId()] = &TYPE_13;

    static const ScriptVarType SYNTH(14, "SYNTH", 'P', BaseVarType::INTEGER, -1);
    scriptVarTypes[SYNTH.getId()] = &SYNTH;

    static const ScriptVarType TYPE_15(15, 'Q', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_15.getId()] = &TYPE_15;

    static const ScriptVarType AREA(16, "AREA", 'R', BaseVarType::INTEGER, -1);
    scriptVarTypes[AREA.getId()] = &AREA;

    static const ScriptVarType STAT(17, "STAT", 'S', BaseVarType::INTEGER, -1);
    scriptVarTypes[STAT.getId()] = &STAT;

    static const ScriptVarType NPC_STAT(18, "NPC_STAT", 'T', BaseVarType::INTEGER, -1);
    scriptVarTypes[NPC_STAT.getId()] = &NPC_STAT;

    static const ScriptVarType WRITEINV(19, "WRITEINV", 'V', BaseVarType::INTEGER, -1);
    scriptVarTypes[WRITEINV.getId()] = &WRITEINV;

    static const ScriptVarType MESH(20, "MESH", '^', BaseVarType::INTEGER, -1);
    scriptVarTypes[MESH.getId()] = &MESH;

    static const ScriptVarType MAPAREA(21, "MAPAREA", '`', BaseVarType::INTEGER, -1);
    scriptVarTypes[MAPAREA.getId()] = &MAPAREA;

    static const ScriptVarType COORDGRID(22, "COORDGRID", 'c', BaseVarType::INTEGER, -1);
    scriptVarTypes[COORDGRID.getId()] = &COORDGRID;

    static const ScriptVarType GRAPHIC(23, "GRAPHIC (SPRITE)", 'd', BaseVarType::INTEGER, -1);
    scriptVarTypes[GRAPHIC.getId()] = &GRAPHIC;

    static const ScriptVarType CHATPHRASE(24, "CHATPHRASE", 'e', BaseVarType::INTEGER, -1);
    scriptVarTypes[CHATPHRASE.getId()] = &CHATPHRASE;

    static const ScriptVarType FONTMETRICS(25, "FONTMETRICS", 'f', BaseVarType::INTEGER, -1);
    scriptVarTypes[FONTMETRICS.getId()] = &FONTMETRICS;

    static const ScriptVarType ENUM(26, "ENUM", 'g', BaseVarType::INTEGER, -1);
    scriptVarTypes[ENUM.getId()] = &ENUM;

    static const ScriptVarType TYPE_27(27, 'h', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_27.getId()] = &TYPE_27;

    static const ScriptVarType JINGLE(28, "JINGLE", 'j', BaseVarType::INTEGER, -1);
    scriptVarTypes[JINGLE.getId()] = &JINGLE;

    static const ScriptVarType CHATCAT(29, "CHATCAT", 'k', BaseVarType::INTEGER, -1);
    scriptVarTypes[CHATCAT.getId()] = &CHATCAT;

    static const ScriptVarType LOC(30, "LOC (OBJECT)", 'l', BaseVarType::INTEGER, -1);
    scriptVarTypes[LOC.getId()] = &LOC;

    static const ScriptVarType MODEL(31, "MODEL", 'm', BaseVarType::INTEGER, -1);
    scriptVarTypes[MODEL.getId()] = &MODEL;

    static const ScriptVarType NPC(32, "NPC", 'n', BaseVarType::INTEGER, -1);
    scriptVarTypes[NPC.getId()] = &NPC;

    static const ScriptVarType OBJ(33, "OBJ (ITEM)", 'o', BaseVarType::INTEGER, -1);
    scriptVarTypes[OBJ.getId()] = &OBJ;

    static const ScriptVarType PLAYER_UID(34, "PLAYER_UID", 'p', BaseVarType::INTEGER, -1);
    scriptVarTypes[PLAYER_UID.getId()] = &PLAYER_UID;

    static const ScriptVarType TYPE_35(35, 'r', BaseVarType::LONG, -1LL);
    scriptVarTypes[TYPE_35.getId()] = &TYPE_35;

    static const ScriptVarType STRING(36, "STRING", 's', BaseVarType::STRING, std::string(""));
    scriptVarTypes[STRING.getId()] = &STRING;

    static const ScriptVarType SPOTANIM(37, "GFX", 't', BaseVarType::INTEGER, -1);
    scriptVarTypes[SPOTANIM.getId()] = &SPOTANIM;

    static const ScriptVarType NPC_UID(38, "NPC_UID", 'u', BaseVarType::INTEGER, -1);
    scriptVarTypes[NPC_UID.getId()] = &NPC_UID;

    static const ScriptVarType INV(39, "INV", 'v', BaseVarType::INTEGER, -1);
    scriptVarTypes[INV.getId()] = &INV;

    static const ScriptVarType TEXTURE(40, "TEXTURE", 'x', BaseVarType::INTEGER, -1);
    scriptVarTypes[TEXTURE.getId()] = &TEXTURE;

    static const ScriptVarType CATEGORY(41, "CATEGORY", 'y', BaseVarType::INTEGER, -1);
    scriptVarTypes[CATEGORY.getId()] = &CATEGORY;

    static const ScriptVarType CHAR(42, "CHAR", 'z', BaseVarType::INTEGER, -1);
    scriptVarTypes[CHAR.getId()] = &CHAR;

    static const ScriptVarType LASER(43, "LASER", '|', BaseVarType::INTEGER, -1);
    scriptVarTypes[LASER.getId()] = &LASER;

    static const ScriptVarType BAS(44, "BAS", U'€', BaseVarType::INTEGER, -1);
    scriptVarTypes[BAS.getId()] = &BAS;

    static const ScriptVarType TYPE_45(45, U'ƒ', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_45.getId()] = &TYPE_45;

    static const ScriptVarType COLLISION_GEOMETRY(46, "COL_GEO", U'‡', BaseVarType::INTEGER, -1);
    scriptVarTypes[COLLISION_GEOMETRY.getId()] = &COLLISION_GEOMETRY;

    static const ScriptVarType PHYSICS_MODEL(47, "PHYS_MODEL", U'‰', BaseVarType::INTEGER, -1);
    scriptVarTypes[PHYSICS_MODEL.getId()] = &PHYSICS_MODEL;

    static const ScriptVarType PHYSICS_CONTROL_MODIFIER(48, "PHYS_CTRL_MOD", U'Š', BaseVarType::INTEGER, -1);
    scriptVarTypes[PHYSICS_CONTROL_MODIFIER.getId()] = &PHYSICS_CONTROL_MODIFIER;

    static const ScriptVarType CLANHASH(49, "CLANHASH", U'Œ', BaseVarType::LONG, -1LL);
    scriptVarTypes[CLANHASH.getId()] = &CLANHASH;

    static const ScriptVarType COORDFINE(50, "COORDFINE", U'Ž', BaseVarType::COORDFINE, nullptr);
    scriptVarTypes[COORDFINE.getId()] = &COORDFINE;

    static const ScriptVarType CUTSCENE(51, "CUTSCENE", U'š', BaseVarType::INTEGER, -1);
    scriptVarTypes[CUTSCENE.getId()] = &CUTSCENE;

    static const ScriptVarType ITEMCODE(53, "ITEMCODE", U'¡', BaseVarType::INTEGER, -1);
    scriptVarTypes[ITEMCODE.getId()] = &ITEMCODE;

    static const ScriptVarType TYPE_54(54, U'¢', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_54.getId()] = &TYPE_54;

    static const ScriptVarType MAPSCENEICON(55, "MAPSCENEICON", U'£', BaseVarType::INTEGER, -1);
    scriptVarTypes[MAPSCENEICON.getId()] = &MAPSCENEICON;

    static const ScriptVarType CLANFORUMQFC(56, "CLANFORUMQFC", U'§', BaseVarType::LONG, -1LL);
    scriptVarTypes[CLANFORUMQFC.getId()] = &CLANFORUMQFC;

    static const ScriptVarType VORBIS(57, "VORBIS", U'«', BaseVarType::INTEGER, -1);
    scriptVarTypes[VORBIS.getId()] = &VORBIS;

    static const ScriptVarType VERIFY_OBJECT(58, "VERIFY_OBJECT", U'®', BaseVarType::INTEGER, -1);
    scriptVarTypes[VERIFY_OBJECT.getId()] = &VERIFY_OBJECT;

    static const ScriptVarType MAPELEMENT(59, "MAPELEMENT", U'µ', BaseVarType::INTEGER, -1);
    scriptVarTypes[MAPELEMENT.getId()] = &MAPELEMENT;

    static const ScriptVarType CATEGORYTYPE(60, "CATEGORYTYPE", U'¶', BaseVarType::INTEGER, -1);
    scriptVarTypes[CATEGORYTYPE.getId()] = &CATEGORYTYPE;

    static const ScriptVarType SOCIAL_NETWORK(61, "SOCIAL_NETWORK", U'Æ', BaseVarType::INTEGER, -1);
    scriptVarTypes[SOCIAL_NETWORK.getId()] = &SOCIAL_NETWORK;

    static const ScriptVarType HITMARK(62, "HITMARK", U'×', BaseVarType::INTEGER, -1);
    scriptVarTypes[HITMARK.getId()] = &HITMARK;

    static const ScriptVarType PACKAGE(63, "PACKAGE", U'Þ', BaseVarType::INTEGER, -1);
    scriptVarTypes[PACKAGE.getId()] = &PACKAGE;

    static const ScriptVarType PARTICLE_EFFECTOR(64, "PARTICLE_EFFECTOR", U'á', BaseVarType::INTEGER, -1);
    scriptVarTypes[PARTICLE_EFFECTOR.getId()] = &PARTICLE_EFFECTOR;

    static const ScriptVarType TYPE_65(65, U'æ', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_65.getId()] = &TYPE_65;

    static const ScriptVarType PARTICLE_EMITTER(66, "PARTICLE_EMITTER", U'é', BaseVarType::INTEGER, -1);
    scriptVarTypes[PARTICLE_EMITTER.getId()] = &PARTICLE_EMITTER;

    static const ScriptVarType PLOGTYPE(67, "PLOGTYPE", U'í', BaseVarType::INTEGER, -1);
    scriptVarTypes[PLOGTYPE.getId()] = &PLOGTYPE;

    static const ScriptVarType UNSIGNED_INT(68, "UINT", U'î', BaseVarType::INTEGER, -1);
    scriptVarTypes[UNSIGNED_INT.getId()] = &UNSIGNED_INT;

    static const ScriptVarType SKYBOX(69, "SKYBOX", U'ó', BaseVarType::INTEGER, -1);
    scriptVarTypes[SKYBOX.getId()] = &SKYBOX;

    static const ScriptVarType SKYDECOR(70, "SKYDECOR", U'ú', BaseVarType::INTEGER, -1);
    scriptVarTypes[SKYDECOR.getId()] = &SKYDECOR;

    static const ScriptVarType HASH64(71, "HASH64", U'û', BaseVarType::LONG, -1LL);
    scriptVarTypes[HASH64.getId()] = &HASH64;

    static const ScriptVarType INPUTTYPE(72, "INPUTTYPE", U'Î', BaseVarType::INTEGER, -1);
    scriptVarTypes[INPUTTYPE.getId()] = &INPUTTYPE;

    static const ScriptVarType STRUCT(73, "STRUCT", 'J', BaseVarType::INTEGER, -1);
    scriptVarTypes[STRUCT.getId()] = &STRUCT;

    static const ScriptVarType DBROW(74, "DBROW", U'Ð', BaseVarType::INTEGER, -1);
    scriptVarTypes[DBROW.getId()] = &DBROW;

    static const ScriptVarType TYPE_75(75, U'¤', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_75.getId()] = &TYPE_75;

    static const ScriptVarType TYPE_76(76, U'¥', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_76.getId()] = &TYPE_76;

    static const ScriptVarType TYPE_77(77, U'è', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_77.getId()] = &TYPE_77;

    static const ScriptVarType TYPE_78(78, U'¹', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_78.getId()] = &TYPE_78;

    static const ScriptVarType TYPE_79(79, U'°', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_79.getId()] = &TYPE_79;

    static const ScriptVarType TYPE_80(80, U'ì', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_80.getId()] = &TYPE_80;

    static const ScriptVarType TYPE_81(81, U'ë', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_81.getId()] = &TYPE_81;

    static const ScriptVarType TYPE_83(83, U'þ', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_83.getId()] = &TYPE_83;

    static const ScriptVarType TYPE_84(84, U'ý', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_84.getId()] = &TYPE_84;

    static const ScriptVarType TYPE_85(85, U'ÿ', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_85.getId()] = &TYPE_85;

    static const ScriptVarType TYPE_86(86, U'õ', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_86.getId()] = &TYPE_86;

    static const ScriptVarType TYPE_87(87, U'ô', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_87.getId()] = &TYPE_87;

    static const ScriptVarType TYPE_88(88, U'ö', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_88.getId()] = &TYPE_88;

    static const ScriptVarType GWC_PLATFORM(89, "GWC_PLATFORM", U'ò', BaseVarType::INTEGER, -1);
    scriptVarTypes[GWC_PLATFORM.getId()] = &GWC_PLATFORM;

    static const ScriptVarType TYPE_90(90, U'Ü', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_90.getId()] = &TYPE_90;

    static const ScriptVarType TYPE_91(91, U'ù', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_91.getId()] = &TYPE_91;

    static const ScriptVarType TYPE_92(92, U'ï', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_92.getId()] = &TYPE_92;

    static const ScriptVarType TYPE_93(93, U'¯', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_93.getId()] = &TYPE_93;

    static const ScriptVarType BUG_TEMPLATE(94, "BUG_TEMPLATE", U'ê', BaseVarType::INTEGER, -1);
    scriptVarTypes[BUG_TEMPLATE.getId()] = &BUG_TEMPLATE;

    static const ScriptVarType BILLING_AUTH_FLAG(95, "BILLING_AUTH_FLAG", U'ð', BaseVarType::INTEGER, -1);
    scriptVarTypes[BILLING_AUTH_FLAG.getId()] = &BILLING_AUTH_FLAG;

    static const ScriptVarType ACCOUNT_FEATURE_FLAG(96, "ACCOUNT_FEATURE_FLAG", U'å', BaseVarType::INTEGER, -1);
    scriptVarTypes[ACCOUNT_FEATURE_FLAG.getId()] = &ACCOUNT_FEATURE_FLAG;

    static const ScriptVarType INTERFACE(97, "INTERFACE", 'a', BaseVarType::INTEGER, -1);
    scriptVarTypes[INTERFACE.getId()] = &INTERFACE;

    static const ScriptVarType TOPLEVELINTERFACE(98, "TOPLEVELITNERFACE", 'F', BaseVarType::INTEGER, -1);
    scriptVarTypes[TOPLEVELINTERFACE.getId()] = &TOPLEVELINTERFACE;

    static const ScriptVarType OVERLAYINTERFACE(99, "OVERLAYINTERFACE", 'L', BaseVarType::INTEGER, -1);
    scriptVarTypes[OVERLAYINTERFACE.getId()] = &OVERLAYINTERFACE;

    static const ScriptVarType CLIENTINTERFACE(100, "CLIENTINTERFACE", U'©', BaseVarType::INTEGER, -1);
    scriptVarTypes[CLIENTINTERFACE.getId()] = &CLIENTINTERFACE;

    static const ScriptVarType MOVESPEED(101, "MOVESPEED", U'Ý', BaseVarType::INTEGER, -1);
    scriptVarTypes[MOVESPEED.getId()] = &MOVESPEED;

    static const ScriptVarType MATERIAL(102, "MATERIAL", U'¬', BaseVarType::INTEGER, -1);
    scriptVarTypes[MATERIAL.getId()] = &MATERIAL;

    static const ScriptVarType SEQGROUP(103, "SEQGROUP", U'ø', BaseVarType::INTEGER, -1);
    scriptVarTypes[SEQGROUP.getId()] = &SEQGROUP;

    static const ScriptVarType TEMP_HISCORE(104, "TEMP_HS", U'ä', BaseVarType::INTEGER, -1);
    scriptVarTypes[TEMP_HISCORE.getId()] = &TEMP_HISCORE;

    static const ScriptVarType TEMP_HISCORE_LENGTH_TYPE(105, "TEMP_HS_LEN", U'ã', BaseVarType::INTEGER, -1);
    scriptVarTypes[TEMP_HISCORE_LENGTH_TYPE.getId()] = &TEMP_HISCORE_LENGTH_TYPE;

    static const ScriptVarType TEMP_HISCORE_DISPLAY_TYPE(106, "TEMP_HS_DISPLAY", U'â', BaseVarType::INTEGER, -1);
    scriptVarTypes[TEMP_HISCORE_DISPLAY_TYPE.getId()] = &TEMP_HISCORE_DISPLAY_TYPE;

    static const ScriptVarType TEMP_HISCORE_CONTRIBUTE_RESULT(107, "TEMPH_HS_CONTRIB_RESULT", U'à',
                                                              BaseVarType::INTEGER, -1);
    scriptVarTypes[TEMP_HISCORE_CONTRIBUTE_RESULT.getId()] = &TEMP_HISCORE_CONTRIBUTE_RESULT;

    static const ScriptVarType AUDIOGROUP(108, "AUDIOGROUP", U'À', BaseVarType::INTEGER, -1);
    scriptVarTypes[AUDIOGROUP.getId()] = &AUDIOGROUP;

    static const ScriptVarType AUDIOMIXBUSS(109, "AUDIOMIXBUS", U'Ò', BaseVarType::INTEGER, -1);
    scriptVarTypes[AUDIOMIXBUSS.getId()] = &AUDIOMIXBUSS;

    static const ScriptVarType LONG(110, "LONG", U'§', BaseVarType::LONG, 0LL);
    scriptVarTypes[LONG.getId()] = &LONG;

    static const ScriptVarType CRM_CHANNEL(111, "CRM_CHANNEL", U'Ì', BaseVarType::INTEGER, -1);
    scriptVarTypes[CRM_CHANNEL.getId()] = &CRM_CHANNEL;

    static const ScriptVarType HTTP_IMAGE(112, "HTTP_IMAGE", U'É', BaseVarType::INTEGER, -1);
    scriptVarTypes[HTTP_IMAGE.getId()] = &HTTP_IMAGE;

    static const ScriptVarType POP_UP_DISPLAY_BEHAVIOUR(113, "POP_UP_DISPLAY_BEHAVIOUR", U'Ê',
                                                        BaseVarType::INTEGER, -1);
    scriptVarTypes[POP_UP_DISPLAY_BEHAVIOUR.getId()] = &POP_UP_DISPLAY_BEHAVIOUR;

    static const ScriptVarType POLL(114, "POLL", U'÷', BaseVarType::INTEGER, -1);
    scriptVarTypes[POLL.getId()] = &POLL;

    static const ScriptVarType TYPE_115(115, U'¼', BaseVarType::LONG, -1LL);
    scriptVarTypes[TYPE_115.getId()] = &TYPE_115;

    static const ScriptVarType TYPE_116(116, U'½', BaseVarType::LONG, -1LL);
    scriptVarTypes[TYPE_116.getId()] = &TYPE_116;

    static const ScriptVarType POINTLIGHT(117, "POINTLIGHT", U'•', BaseVarType::INTEGER, -1);
    scriptVarTypes[POINTLIGHT.getId()] = &POINTLIGHT;

    static const ScriptVarType PLAYER_GROUP(118, "PLAYER_GROUP", U'Â', BaseVarType::LONG, -1LL);
    scriptVarTypes[PLAYER_GROUP.getId()] = &PLAYER_GROUP;

    static const ScriptVarType PLAYER_GROUP_STATUS(119, "PLAYER_GROUP_STATUS", U'Ã', BaseVarType::INTEGER, -1);
    scriptVarTypes[PLAYER_GROUP_STATUS.getId()] = &PLAYER_GROUP_STATUS;

    static const ScriptVarType PLAYER_GROUP_INVITE_RESULT(120, "PLAYER_GROUP_INVITE_RESULT", U'Å',
                                                          BaseVarType::INTEGER, -1);
    scriptVarTypes[PLAYER_GROUP_INVITE_RESULT.getId()] = &PLAYER_GROUP_INVITE_RESULT;

    static const ScriptVarType PLAYER_GROUP_MODIFY_RESULT(121, "PLAYER_GROUP_MODIFY_RESULT", U'Ë',
                                                          BaseVarType::INTEGER, -1);
    scriptVarTypes[PLAYER_GROUP_MODIFY_RESULT.getId()] = &PLAYER_GROUP_MODIFY_RESULT;

    static const ScriptVarType PLAYER_GROUP_JOIN_OR_CREATE_RESULT(122, "PLAYER_GROU_JOIN_OR_CREATE_RESULT", U'Í',
                                                                  BaseVarType::INTEGER, -1);
    scriptVarTypes[PLAYER_GROUP_JOIN_OR_CREATE_RESULT.getId()] = &PLAYER_GROUP_JOIN_OR_CREATE_RESULT;

    static const ScriptVarType PLAYER_GROUP_AFFINITY_MODIFY_RESULT(123, "PLAYER_GROU_AFFINITY_MODIFY_RESULT", U'Õ',
                                                                   BaseVarType::INTEGER, -1);
    scriptVarTypes[PLAYER_GROUP_AFFINITY_MODIFY_RESULT.getId()] = &PLAYER_GROUP_AFFINITY_MODIFY_RESULT;

    static const ScriptVarType PLAYER_GROUP_DELTA_TYPE(124, "PLAYER_GROUP_DELTA_TYPE", U'²', BaseVarType::INTEGER,
                                                       -1);
    scriptVarTypes[PLAYER_GROUP_DELTA_TYPE.getId()] = &PLAYER_GROUP_DELTA_TYPE;

    static const ScriptVarType CLIENT_TYPE(125, "CLIENT_TYPE", U'ª', BaseVarType::INTEGER, -1);
    scriptVarTypes[CLIENT_TYPE.getId()] = &CLIENT_TYPE;

    static const ScriptVarType TELEMETRY_INTERVAL(126, "TELEMETRY_INTERVAL", '\0', BaseVarType::INTEGER, 0);
    scriptVarTypes[TELEMETRY_INTERVAL.getId()] = &TELEMETRY_INTERVAL;

    static const ScriptVarType WORLD_AREA(127, "WORLD_AREA", '\0', BaseVarType::INTEGER, 0);
    scriptVarTypes[WORLD_AREA.getId()] = &WORLD_AREA;

    static const ScriptVarType TYPE_128(128, '\0', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_128.getId()] = &TYPE_128;

    static const ScriptVarType TYPE_129(129, U'Ø', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_129.getId()] = &TYPE_129;

    static const ScriptVarType TYPE_130(130, '\0', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_130.getId()] = &TYPE_130;

    static const ScriptVarType TYPE_131(131, U'˜', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_131.getId()] = &TYPE_131;

    static const ScriptVarType TYPE_132(132, "interface_skin", U'˜', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_132.getId()] = &TYPE_132;

    static const ScriptVarType TYPE_133(133, "unknown", U'˜', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_133.getId()] = &TYPE_133;

    static const ScriptVarType TYPE_138(138, "unknown138", 'i', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_138.getId()] = &TYPE_138;

    static const ScriptVarType TYPE_200(200, 'X', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_200.getId()] = &TYPE_200;

    static const ScriptVarType TYPE_201(201, 'W', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_201.getId()] = &TYPE_201;

    static const ScriptVarType TYPE_202(202, 'b', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_202.getId()] = &TYPE_202;

    static const ScriptVarType TYPE_203(203, 'B', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_203.getId()] = &TYPE_203;

    static const ScriptVarType TYPE_204(204, '4', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_204.getId()] = &TYPE_204;

    static const ScriptVarType TYPE_205(205, 'w', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_205.getId()] = &TYPE_205;

    static const ScriptVarType TYPE_206(206, 'q', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_206.getId()] = &TYPE_206;

    static const ScriptVarType TYPE_207(207, '0', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_207.getId()] = &TYPE_207;

    static const ScriptVarType TYPE_208(208, '6', BaseVarType::INTEGER, -1);
    scriptVarTypes[TYPE_208.getId()] = &TYPE_208;

    static const ScriptVarType VAR_TYPE(209, "VAR_TYPE", '7', BaseVarType::INTEGER, -1);
    scriptVarTypes[VAR_TYPE.getId()] = &VAR_TYPE;

    initialized = true;
}

ScriptVarType *ScriptVarType::getScriptVarTypeById(int id)
{
    if (!initialized)
        init();
    auto it = scriptVarTypes.find(id);
    if (it == scriptVarTypes.end())
    {
        return nullptr;
    }
    return const_cast<ScriptVarType *>(it->second);
}

ScriptVarType *ScriptVarType::getByChar(char32_t c)
{
    if (!initialized)
        init();
    for (const auto &kv: scriptVarTypes)
    {
        if (kv.second->c == c)
            return const_cast<ScriptVarType *>(kv.second);
    }
    return nullptr;
}

static const char32_t CP1252_SPECIAL_CHARS_TO_UNICODE_MAP[32] = {
    U'€', U'\0', U'‚', U'ƒ', U'„', U'…', U'†', U'‡',
    U'ˆ', U'‰', U'Š', U'‹', U'Œ', U'\0', U'Ž', U'\0',
    U'\0', U'‘', U'’', U'“', U'”', U'•', U'–', U'—',
    U'˜', U'™', U'š', U'›', U'œ', U'\0', U'ž', U'Ÿ'
};

char32_t ScriptVarType::cp1252ToUnicode(char cp1252Char)
{
    auto uch = static_cast<unsigned char>(cp1252Char);
    if (uch < 0x80)
    {
        return static_cast<char32_t>(uch);
    }
    else if (uch < 0xA0)
    {
        char32_t mapped = CP1252_SPECIAL_CHARS_TO_UNICODE_MAP[uch - 0x80];
        return mapped ? mapped : static_cast<char32_t>(uch);
    }
    else
    {
        return static_cast<char32_t>(uch);
    }
}
