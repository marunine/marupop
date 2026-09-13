// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// GENERATED FILE - do not edit by hand.
// Generator: tools/gen-jp-tables.py
// Ported from JL (Apache-2.0), JL.Core/Japanese/JapaneseUtils.cs at commit 85ae02eeb84f378387f48c12e7b468390a9f2007.
// Every table below is one of that file's FrozenDictionary or SearchValues fields,
// sorted by key so jp/japanese.cpp can binary-search it.
#pragma once

#include <array>
#include <cstdint>

namespace maru::jp::tables
{

struct CharPair
{
    char16_t from;
    char16_t to;
};

struct SupplementaryPair
{
    char32_t from;
    char16_t to;
};

// s_normalizationDict: katakana to hiragana, the hiragana wi/we fold, the CJK radical
// supplement to kanji block, and the kyuujitai to shinjitai block.
inline constexpr std::array<CharPair, 536> kNormalization{{
    {.from = 0x2E81, .to = 0x5382}, // ⺁ -> 厂
    {.from = 0x2E82, .to = 0x4E5B}, // ⺂ -> 乛
    {.from = 0x2E83, .to = 0x4E5A}, // ⺃ -> 乚
    {.from = 0x2E84, .to = 0x4E59}, // ⺄ -> 乙
    {.from = 0x2E85, .to = 0x4EBB}, // ⺅ -> 亻
    {.from = 0x2E86, .to = 0x5182}, // ⺆ -> 冂
    {.from = 0x2E87, .to = 0x51E0}, // ⺇ -> 几
    {.from = 0x2E88, .to = 0x5200}, // ⺈ -> 刀
    {.from = 0x2E89, .to = 0x5202}, // ⺉ -> 刂
    {.from = 0x2E8A, .to = 0x535C}, // ⺊ -> 卜
    {.from = 0x2E8B, .to = 0x353E}, // ⺋ -> 㔾
    {.from = 0x2E8C, .to = 0x5C0F}, // ⺌ -> 小
    {.from = 0x2E8D, .to = 0x5C0F}, // ⺍ -> 小
    {.from = 0x2E8E, .to = 0x5140}, // ⺎ -> 兀
    {.from = 0x2E8F, .to = 0x5C23}, // ⺏ -> 尣
    {.from = 0x2E90, .to = 0x5C22}, // ⺐ -> 尢
    {.from = 0x2E91, .to = 0x5C23}, // ⺑ -> 尣
    {.from = 0x2E92, .to = 0x5DF3}, // ⺒ -> 巳
    {.from = 0x2E93, .to = 0x5E7A}, // ⺓ -> 幺
    {.from = 0x2E94, .to = 0x5F51}, // ⺔ -> 彑
    {.from = 0x2E95, .to = 0x5F50}, // ⺕ -> 彐
    {.from = 0x2E96, .to = 0x5FC4}, // ⺖ -> 忄
    {.from = 0x2E97, .to = 0x38FA}, // ⺗ -> 㣺
    {.from = 0x2E98, .to = 0x624C}, // ⺘ -> 扌
    {.from = 0x2E99, .to = 0x6535}, // ⺙ -> 攵
    {.from = 0x2E9B, .to = 0x65E1}, // ⺛ -> 旡
    {.from = 0x2E9C, .to = 0x65E5}, // ⺜ -> 日
    {.from = 0x2E9D, .to = 0x6708}, // ⺝ -> 月
    {.from = 0x2E9E, .to = 0x6B7A}, // ⺞ -> 歺
    {.from = 0x2EA0, .to = 0x6C11}, // ⺠ -> 民
    {.from = 0x2EA1, .to = 0x6C35}, // ⺡ -> 氵
    {.from = 0x2EA2, .to = 0x6C3A}, // ⺢ -> 氺
    {.from = 0x2EA3, .to = 0x706C}, // ⺣ -> 灬
    {.from = 0x2EA4, .to = 0x722B}, // ⺤ -> 爫
    {.from = 0x2EA5, .to = 0x722B}, // ⺥ -> 爫
    {.from = 0x2EA6, .to = 0x4E2C}, // ⺦ -> 丬
    {.from = 0x2EA7, .to = 0x725B}, // ⺧ -> 牛
    {.from = 0x2EA8, .to = 0x72AD}, // ⺨ -> 犭
    {.from = 0x2EA9, .to = 0x738B}, // ⺩ -> 王
    {.from = 0x2EAA, .to = 0x758B}, // ⺪ -> 疋
    {.from = 0x2EAB, .to = 0x7F52}, // ⺫ -> 罒
    {.from = 0x2EAC, .to = 0x793A}, // ⺬ -> 示
    {.from = 0x2EAD, .to = 0x793B}, // ⺭ -> 礻
    {.from = 0x2EAE, .to = 0x7AF9}, // ⺮ -> 竹
    {.from = 0x2EAF, .to = 0x7CF9}, // ⺯ -> 糹
    {.from = 0x2EB0, .to = 0x7E9F}, // ⺰ -> 纟
    {.from = 0x2EB1, .to = 0x7F53}, // ⺱ -> 罓
    {.from = 0x2EB2, .to = 0x7F52}, // ⺲ -> 罒
    {.from = 0x2EB3, .to = 0x34C1}, // ⺳ -> 㓁
    {.from = 0x2EB4, .to = 0x34C1}, // ⺴ -> 㓁
    {.from = 0x2EB5, .to = 0x7F51}, // ⺵ -> 网
    {.from = 0x2EB6, .to = 0x7F8A}, // ⺶ -> 羊
    {.from = 0x2EB7, .to = 0x7F8A}, // ⺷ -> 羊
    {.from = 0x2EB8, .to = 0x7F8B}, // ⺸ -> 羋
    {.from = 0x2EB9, .to = 0x8002}, // ⺹ -> 耂
    {.from = 0x2EBA, .to = 0x8080}, // ⺺ -> 肀
    {.from = 0x2EBB, .to = 0x807F}, // ⺻ -> 聿
    {.from = 0x2EBC, .to = 0x6708}, // ⺼ -> 月
    {.from = 0x2EBD, .to = 0x81FC}, // ⺽ -> 臼
    {.from = 0x2EBE, .to = 0x8279}, // ⺾ -> 艹
    {.from = 0x2EBF, .to = 0x8279}, // ⺿ -> 艹
    {.from = 0x2EC0, .to = 0x8279}, // ⻀ -> 艹
    {.from = 0x2EC1, .to = 0x864E}, // ⻁ -> 虎
    {.from = 0x2EC2, .to = 0x8863}, // ⻂ -> 衣
    {.from = 0x2EC3, .to = 0x8980}, // ⻃ -> 覀
    {.from = 0x2EC4, .to = 0x897F}, // ⻄ -> 西
    {.from = 0x2EC5, .to = 0x89C1}, // ⻅ -> 见
    {.from = 0x2EC6, .to = 0x89D2}, // ⻆ -> 角
    {.from = 0x2EC7, .to = 0x89D2}, // ⻇ -> 角
    {.from = 0x2EC8, .to = 0x8BA0}, // ⻈ -> 讠
    {.from = 0x2EC9, .to = 0x8D1D}, // ⻉ -> 贝
    {.from = 0x2ECA, .to = 0x8DB3}, // ⻊ -> 足
    {.from = 0x2ECB, .to = 0x8F66}, // ⻋ -> 车
    {.from = 0x2ECC, .to = 0x8FB6}, // ⻌ -> 辶
    {.from = 0x2ECD, .to = 0x8FB6}, // ⻍ -> 辶
    {.from = 0x2ECE, .to = 0x8FB6}, // ⻎ -> 辶
    {.from = 0x2ECF, .to = 0x961D}, // ⻏ -> 阝
    {.from = 0x2ED0, .to = 0x9485}, // ⻐ -> 钅
    {.from = 0x2ED1, .to = 0x9577}, // ⻑ -> 長
    {.from = 0x2ED2, .to = 0x9578}, // ⻒ -> 镸
    {.from = 0x2ED3, .to = 0x957F}, // ⻓ -> 长
    {.from = 0x2ED4, .to = 0x95E8}, // ⻔ -> 门
    {.from = 0x2ED5, .to = 0x961C}, // ⻕ -> 阜
    {.from = 0x2ED6, .to = 0x961D}, // ⻖ -> 阝
    {.from = 0x2ED7, .to = 0x96E8}, // ⻗ -> 雨
    {.from = 0x2ED8, .to = 0x9752}, // ⻘ -> 青
    {.from = 0x2ED9, .to = 0x97E6}, // ⻙ -> 韦
    {.from = 0x2EDA, .to = 0x9875}, // ⻚ -> 页
    {.from = 0x2EDB, .to = 0x98CE}, // ⻛ -> 风
    {.from = 0x2EDC, .to = 0x98DE}, // ⻜ -> 飞
    {.from = 0x2EDD, .to = 0x98DF}, // ⻝ -> 食
    {.from = 0x2EDE, .to = 0x98DF}, // ⻞ -> 食
    {.from = 0x2EDF, .to = 0x98E0}, // ⻟ -> 飠
    {.from = 0x2EE0, .to = 0x9963}, // ⻠ -> 饣
    {.from = 0x2EE1, .to = 0x9996}, // ⻡ -> 首
    {.from = 0x2EE2, .to = 0x9A6C}, // ⻢ -> 马
    {.from = 0x2EE3, .to = 0x9AA8}, // ⻣ -> 骨
    {.from = 0x2EE4, .to = 0x9B3C}, // ⻤ -> 鬼
    {.from = 0x2EE5, .to = 0x9C7C}, // ⻥ -> 鱼
    {.from = 0x2EE6, .to = 0x9E1F}, // ⻦ -> 鸟
    {.from = 0x2EE7, .to = 0x5364}, // ⻧ -> 卤
    {.from = 0x2EE8, .to = 0x9EA6}, // ⻨ -> 麦
    {.from = 0x2EE9, .to = 0x9EC4}, // ⻩ -> 黄
    {.from = 0x2EEA, .to = 0x9EFE}, // ⻪ -> 黾
    {.from = 0x2EEB, .to = 0x6589}, // ⻫ -> 斉
    {.from = 0x2EEC, .to = 0x9F50}, // ⻬ -> 齐
    {.from = 0x2EED, .to = 0x6B6F}, // ⻭ -> 歯
    {.from = 0x2EEE, .to = 0x9F7F}, // ⻮ -> 齿
    {.from = 0x2EEF, .to = 0x7ADC}, // ⻯ -> 竜
    {.from = 0x2EF0, .to = 0x9F99}, // ⻰ -> 龙
    {.from = 0x2EF1, .to = 0x9F9C}, // ⻱ -> 龜
    {.from = 0x2EF2, .to = 0x4E80}, // ⻲ -> 亀
    {.from = 0x2EF3, .to = 0x4E80}, // ⻳ -> 亀
    {.from = 0x3090, .to = 0x3044}, // ゐ -> い
    {.from = 0x3091, .to = 0x3048}, // ゑ -> え
    {.from = 0x30A1, .to = 0x3041}, // ァ -> ぁ
    {.from = 0x30A2, .to = 0x3042}, // ア -> あ
    {.from = 0x30A3, .to = 0x3043}, // ィ -> ぃ
    {.from = 0x30A4, .to = 0x3044}, // イ -> い
    {.from = 0x30A5, .to = 0x3045}, // ゥ -> ぅ
    {.from = 0x30A6, .to = 0x3046}, // ウ -> う
    {.from = 0x30A7, .to = 0x3047}, // ェ -> ぇ
    {.from = 0x30A8, .to = 0x3048}, // エ -> え
    {.from = 0x30A9, .to = 0x3049}, // ォ -> ぉ
    {.from = 0x30AA, .to = 0x304A}, // オ -> お
    {.from = 0x30AB, .to = 0x304B}, // カ -> か
    {.from = 0x30AC, .to = 0x304C}, // ガ -> が
    {.from = 0x30AD, .to = 0x304D}, // キ -> き
    {.from = 0x30AE, .to = 0x304E}, // ギ -> ぎ
    {.from = 0x30AF, .to = 0x304F}, // ク -> く
    {.from = 0x30B0, .to = 0x3050}, // グ -> ぐ
    {.from = 0x30B1, .to = 0x3051}, // ケ -> け
    {.from = 0x30B2, .to = 0x3052}, // ゲ -> げ
    {.from = 0x30B3, .to = 0x3053}, // コ -> こ
    {.from = 0x30B4, .to = 0x3054}, // ゴ -> ご
    {.from = 0x30B5, .to = 0x3055}, // サ -> さ
    {.from = 0x30B6, .to = 0x3056}, // ザ -> ざ
    {.from = 0x30B7, .to = 0x3057}, // シ -> し
    {.from = 0x30B8, .to = 0x3058}, // ジ -> じ
    {.from = 0x30B9, .to = 0x3059}, // ス -> す
    {.from = 0x30BA, .to = 0x305A}, // ズ -> ず
    {.from = 0x30BB, .to = 0x305B}, // セ -> せ
    {.from = 0x30BC, .to = 0x305C}, // ゼ -> ぜ
    {.from = 0x30BD, .to = 0x305D}, // ソ -> そ
    {.from = 0x30BE, .to = 0x305E}, // ゾ -> ぞ
    {.from = 0x30BF, .to = 0x305F}, // タ -> た
    {.from = 0x30C0, .to = 0x3060}, // ダ -> だ
    {.from = 0x30C1, .to = 0x3061}, // チ -> ち
    {.from = 0x30C2, .to = 0x3062}, // ヂ -> ぢ
    {.from = 0x30C3, .to = 0x3063}, // ッ -> っ
    {.from = 0x30C4, .to = 0x3064}, // ツ -> つ
    {.from = 0x30C5, .to = 0x3065}, // ヅ -> づ
    {.from = 0x30C6, .to = 0x3066}, // テ -> て
    {.from = 0x30C7, .to = 0x3067}, // デ -> で
    {.from = 0x30C8, .to = 0x3068}, // ト -> と
    {.from = 0x30C9, .to = 0x3069}, // ド -> ど
    {.from = 0x30CA, .to = 0x306A}, // ナ -> な
    {.from = 0x30CB, .to = 0x306B}, // ニ -> に
    {.from = 0x30CC, .to = 0x306C}, // ヌ -> ぬ
    {.from = 0x30CD, .to = 0x306D}, // ネ -> ね
    {.from = 0x30CE, .to = 0x306E}, // ノ -> の
    {.from = 0x30CF, .to = 0x306F}, // ハ -> は
    {.from = 0x30D0, .to = 0x3070}, // バ -> ば
    {.from = 0x30D1, .to = 0x3071}, // パ -> ぱ
    {.from = 0x30D2, .to = 0x3072}, // ヒ -> ひ
    {.from = 0x30D3, .to = 0x3073}, // ビ -> び
    {.from = 0x30D4, .to = 0x3074}, // ピ -> ぴ
    {.from = 0x30D5, .to = 0x3075}, // フ -> ふ
    {.from = 0x30D6, .to = 0x3076}, // ブ -> ぶ
    {.from = 0x30D7, .to = 0x3077}, // プ -> ぷ
    {.from = 0x30D8, .to = 0x3078}, // ヘ -> へ
    {.from = 0x30D9, .to = 0x3079}, // ベ -> べ
    {.from = 0x30DA, .to = 0x307A}, // ペ -> ぺ
    {.from = 0x30DB, .to = 0x307B}, // ホ -> ほ
    {.from = 0x30DC, .to = 0x307C}, // ボ -> ぼ
    {.from = 0x30DD, .to = 0x307D}, // ポ -> ぽ
    {.from = 0x30DE, .to = 0x307E}, // マ -> ま
    {.from = 0x30DF, .to = 0x307F}, // ミ -> み
    {.from = 0x30E0, .to = 0x3080}, // ム -> む
    {.from = 0x30E1, .to = 0x3081}, // メ -> め
    {.from = 0x30E2, .to = 0x3082}, // モ -> も
    {.from = 0x30E3, .to = 0x3083}, // ャ -> ゃ
    {.from = 0x30E4, .to = 0x3084}, // ヤ -> や
    {.from = 0x30E5, .to = 0x3085}, // ュ -> ゅ
    {.from = 0x30E6, .to = 0x3086}, // ユ -> ゆ
    {.from = 0x30E7, .to = 0x3087}, // ョ -> ょ
    {.from = 0x30E8, .to = 0x3088}, // ヨ -> よ
    {.from = 0x30E9, .to = 0x3089}, // ラ -> ら
    {.from = 0x30EA, .to = 0x308A}, // リ -> り
    {.from = 0x30EB, .to = 0x308B}, // ル -> る
    {.from = 0x30EC, .to = 0x308C}, // レ -> れ
    {.from = 0x30ED, .to = 0x308D}, // ロ -> ろ
    {.from = 0x30EE, .to = 0x308E}, // ヮ -> ゎ
    {.from = 0x30EF, .to = 0x308F}, // ワ -> わ
    {.from = 0x30F0, .to = 0x3044}, // ヰ -> い
    {.from = 0x30F1, .to = 0x3048}, // ヱ -> え
    {.from = 0x30F2, .to = 0x3092}, // ヲ -> を
    {.from = 0x30F3, .to = 0x3093}, // ン -> ん
    {.from = 0x30F4, .to = 0x3094}, // ヴ -> ゔ
    {.from = 0x30F5, .to = 0x3095}, // ヵ -> ゕ
    {.from = 0x30F6, .to = 0x3096}, // ヶ -> ゖ
    {.from = 0x30FD, .to = 0x309D}, // ヽ -> ゝ
    {.from = 0x30FE, .to = 0x309E}, // ヾ -> ゞ
    {.from = 0x4E58, .to = 0x4E57}, // 乘 -> 乗
    {.from = 0x4E82, .to = 0x4E71}, // 亂 -> 乱
    {.from = 0x4E9E, .to = 0x4E9C}, // 亞 -> 亜
    {.from = 0x4F5B, .to = 0x4ECF}, // 佛 -> 仏
    {.from = 0x4F86, .to = 0x6765}, // 來 -> 来
    {.from = 0x5002, .to = 0x4F75}, // 倂 -> 併
    {.from = 0x5047, .to = 0x4EEE}, // 假 -> 仮
    {.from = 0x50B3, .to = 0x4F1D}, // 傳 -> 伝
    {.from = 0x50DE, .to = 0x507D}, // 僞 -> 偽
    {.from = 0x50F9, .to = 0x4FA1}, // 價 -> 価
    {.from = 0x5109, .to = 0x5039}, // 儉 -> 倹
    {.from = 0x5152, .to = 0x5150}, // 兒 -> 児
    {.from = 0x5167, .to = 0x5185}, // 內 -> 内
    {.from = 0x5169, .to = 0x4E21}, // 兩 -> 両
    {.from = 0x518C, .to = 0x518A}, // 册 -> 冊
    {.from = 0x5269, .to = 0x5270}, // 剩 -> 剰
    {.from = 0x528D, .to = 0x5263}, // 劍 -> 剣
    {.from = 0x5291, .to = 0x5264}, // 劑 -> 剤
    {.from = 0x52DE, .to = 0x52B4}, // 勞 -> 労
    {.from = 0x52F3, .to = 0x52F2}, // 勳 -> 勲
    {.from = 0x52F5, .to = 0x52B1}, // 勵 -> 励
    {.from = 0x52F8, .to = 0x52E7}, // 勸 -> 勧
    {.from = 0x5340, .to = 0x533A}, // 區 -> 区
    {.from = 0x5377, .to = 0x5DFB}, // 卷 -> 巻
    {.from = 0x537D, .to = 0x5373}, // 卽 -> 即
    {.from = 0x53C3, .to = 0x53C2}, // 參 -> 参
    {.from = 0x5433, .to = 0x5449}, // 吳 -> 呉
    {.from = 0x543F, .to = 0x544A}, // 吿 -> 告
    {.from = 0x55AE, .to = 0x5358}, // 單 -> 単
    {.from = 0x56B4, .to = 0x53B3}, // 嚴 -> 厳
    {.from = 0x56D1, .to = 0x5631}, // 囑 -> 嘱
    {.from = 0x5708, .to = 0x570F}, // 圈 -> 圏
    {.from = 0x570B, .to = 0x56FD}, // 國 -> 国
    {.from = 0x570D, .to = 0x56F2}, // 圍 -> 囲
    {.from = 0x5713, .to = 0x5186}, // 圓 -> 円
    {.from = 0x5716, .to = 0x56F3}, // 圖 -> 図
    {.from = 0x5718, .to = 0x56E3}, // 團 -> 団
    {.from = 0x582F, .to = 0x5C2D}, // 堯 -> 尭
    {.from = 0x589E, .to = 0x5897}, // 增 -> 増
    {.from = 0x58AE, .to = 0x5815}, // 墮 -> 堕
    {.from = 0x58D3, .to = 0x5727}, // 壓 -> 圧
    {.from = 0x58D8, .to = 0x5841}, // 壘 -> 塁
    {.from = 0x58DE, .to = 0x58CA}, // 壞 -> 壊
    {.from = 0x58E4, .to = 0x58CC}, // 壤 -> 壌
    {.from = 0x58EF, .to = 0x58EE}, // 壯 -> 壮
    {.from = 0x58F9, .to = 0x58F1}, // 壹 -> 壱
    {.from = 0x58FB, .to = 0x5A7F}, // 壻 -> 婿
    {.from = 0x58FD, .to = 0x5BFF}, // 壽 -> 寿
    {.from = 0x5967, .to = 0x5965}, // 奧 -> 奥
    {.from = 0x596C, .to = 0x5968}, // 奬 -> 奨
    {.from = 0x59EC, .to = 0x59EB}, // 姬 -> 姫
    {.from = 0x5A1B, .to = 0x5A2F}, // 娛 -> 娯
    {.from = 0x5B43, .to = 0x5B22}, // 孃 -> 嬢
    {.from = 0x5B78, .to = 0x5B66}, // 學 -> 学
    {.from = 0x5BE2, .to = 0x5BDD}, // 寢 -> 寝
    {.from = 0x5BE6, .to = 0x5B9F}, // 實 -> 実
    {.from = 0x5BEB, .to = 0x5199}, // 寫 -> 写
    {.from = 0x5BEC, .to = 0x5BDB}, // 寬 -> 寛
    {.from = 0x5BF6, .to = 0x5B9D}, // 寶 -> 宝
    {.from = 0x5C07, .to = 0x5C06}, // 將 -> 将
    {.from = 0x5C08, .to = 0x5C02}, // 專 -> 専
    {.from = 0x5C0D, .to = 0x5BFE}, // 對 -> 対
    {.from = 0x5C19, .to = 0x5C1A}, // 尙 -> 尚
    {.from = 0x5C46, .to = 0x5C4A}, // 屆 -> 届
    {.from = 0x5C6C, .to = 0x5C5E}, // 屬 -> 属
    {.from = 0x5CFD, .to = 0x5CE1}, // 峽 -> 峡
    {.from = 0x5DBD, .to = 0x5CB3}, // 嶽 -> 岳
    {.from = 0x5DD6, .to = 0x5DCC}, // 巖 -> 巌
    {.from = 0x5DE2, .to = 0x5DE3}, // 巢 -> 巣
    {.from = 0x5E36, .to = 0x5E2F}, // 帶 -> 帯
    {.from = 0x5EDA, .to = 0x53A8}, // 廚 -> 厨
    {.from = 0x5EE2, .to = 0x5EC3}, // 廢 -> 廃
    {.from = 0x5EE3, .to = 0x5E83}, // 廣 -> 広
    {.from = 0x5EF3, .to = 0x5E81}, // 廳 -> 庁
    {.from = 0x5F48, .to = 0x5F3E}, // 彈 -> 弾
    {.from = 0x5F4C, .to = 0x5F25}, // 彌 -> 弥
    {.from = 0x5F91, .to = 0x5F84}, // 徑 -> 径
    {.from = 0x5F9E, .to = 0x5F93}, // 從 -> 従
    {.from = 0x5FB5, .to = 0x5FB4}, // 徵 -> 徴
    {.from = 0x5FB7, .to = 0x5FB3}, // 德 -> 徳
    {.from = 0x6046, .to = 0x6052}, // 恆 -> 恒
    {.from = 0x6085, .to = 0x60A6}, // 悅 -> 悦
    {.from = 0x60E0, .to = 0x6075}, // 惠 -> 恵
    {.from = 0x60E1, .to = 0x60AA}, // 惡 -> 悪
    {.from = 0x60F1, .to = 0x60A9}, // 惱 -> 悩
    {.from = 0x613C, .to = 0x614E}, // 愼 -> 慎
    {.from = 0x6158, .to = 0x60E8}, // 慘 -> 惨
    {.from = 0x61C9, .to = 0x5FDC}, // 應 -> 応
    {.from = 0x61F7, .to = 0x61D0}, // 懷 -> 懐
    {.from = 0x6200, .to = 0x604B}, // 戀 -> 恋
    {.from = 0x6230, .to = 0x6226}, // 戰 -> 戦
    {.from = 0x6232, .to = 0x622F}, // 戲 -> 戯
    {.from = 0x6236, .to = 0x6238}, // 戶 -> 戸
    {.from = 0x623E, .to = 0x623B}, // 戾 -> 戻
    {.from = 0x62C2, .to = 0x6255}, // 拂 -> 払
    {.from = 0x62D4, .to = 0x629C}, // 拔 -> 抜
    {.from = 0x62DC, .to = 0x62DD}, // 拜 -> 拝
    {.from = 0x633E, .to = 0x631F}, // 挾 -> 挟
    {.from = 0x63D2, .to = 0x633F}, // 插 -> 挿
    {.from = 0x63ED, .to = 0x63B2}, // 揭 -> 掲
    {.from = 0x6416, .to = 0x63FA}, // 搖 -> 揺
    {.from = 0x641C, .to = 0x635C}, // 搜 -> 捜
    {.from = 0x64C7, .to = 0x629E}, // 擇 -> 択
    {.from = 0x64CA, .to = 0x6483}, // 擊 -> 撃
    {.from = 0x64D4, .to = 0x62C5}, // 擔 -> 担
    {.from = 0x64DA, .to = 0x62E0}, // 據 -> 拠
    {.from = 0x64E7, .to = 0x6319}, // 擧 -> 挙
    {.from = 0x64F4, .to = 0x62E1}, // 擴 -> 拡
    {.from = 0x651D, .to = 0x6442}, // 攝 -> 摂
    {.from = 0x6536, .to = 0x53CE}, // 收 -> 収
    {.from = 0x6548, .to = 0x52B9}, // 效 -> 効
    {.from = 0x654D, .to = 0x53D9}, // 敍 -> 叙
    {.from = 0x654E, .to = 0x6559}, // 敎 -> 教
    {.from = 0x6555, .to = 0x52C5}, // 敕 -> 勅
    {.from = 0x6578, .to = 0x6570}, // 數 -> 数
    {.from = 0x65B7, .to = 0x65AD}, // 斷 -> 断
    {.from = 0x6649, .to = 0x664B}, // 晉 -> 晋
    {.from = 0x665A, .to = 0x6669}, // 晚 -> 晩
    {.from = 0x665D, .to = 0x663C}, // 晝 -> 昼
    {.from = 0x66C6, .to = 0x66A6}, // 曆 -> 暦
    {.from = 0x66C9, .to = 0x6681}, // 曉 -> 暁
    {.from = 0x66FE, .to = 0x66FD}, // 曾 -> 曽
    {.from = 0x6703, .to = 0x4F1A}, // 會 -> 会
    {.from = 0x689D, .to = 0x6761}, // 條 -> 条
    {.from = 0x68E7, .to = 0x685F}, // 棧 -> 桟
    {.from = 0x69AE, .to = 0x6804}, // 榮 -> 栄
    {.from = 0x69C7, .to = 0x69D9}, // 槇 -> 槙
    {.from = 0x6A02, .to = 0x697D}, // 樂 -> 楽
    {.from = 0x6A13, .to = 0x697C}, // 樓 -> 楼
    {.from = 0x6A1E, .to = 0x67A2}, // 樞 -> 枢
    {.from = 0x6A23, .to = 0x69D8}, // 樣 -> 様
    {.from = 0x6A6B, .to = 0x6A2A}, // 橫 -> 横
    {.from = 0x6AA2, .to = 0x691C}, // 檢 -> 検
    {.from = 0x6AFB, .to = 0x685C}, // 櫻 -> 桜
    {.from = 0x6B0A, .to = 0x6A29}, // 權 -> 権
    {.from = 0x6B50, .to = 0x6B27}, // 歐 -> 欧
    {.from = 0x6B61, .to = 0x6B53}, // 歡 -> 歓
    {.from = 0x6B65, .to = 0x6B69}, // 步 -> 歩
    {.from = 0x6B72, .to = 0x6B73}, // 歲 -> 歳
    {.from = 0x6B77, .to = 0x6B74}, // 歷 -> 歴
    {.from = 0x6B78, .to = 0x5E30}, // 歸 -> 帰
    {.from = 0x6B98, .to = 0x6B8B}, // 殘 -> 残
    {.from = 0x6BBC, .to = 0x6BBB}, // 殼 -> 殻
    {.from = 0x6BC6, .to = 0x6BB4}, // 毆 -> 殴
    {.from = 0x6BCF, .to = 0x6BCE}, // 每 -> 毎
    {.from = 0x6C23, .to = 0x6C17}, // 氣 -> 気
    {.from = 0x6C92, .to = 0x6CA1}, // 沒 -> 没
    {.from = 0x6D89, .to = 0x6E09}, // 涉 -> 渉
    {.from = 0x6DDA, .to = 0x6D99}, // 淚 -> 涙
    {.from = 0x6DE8, .to = 0x6D44}, // 淨 -> 浄
    {.from = 0x6DFA, .to = 0x6D45}, // 淺 -> 浅
    {.from = 0x6E34, .to = 0x6E07}, // 渴 -> 渇
    {.from = 0x6EAA, .to = 0x6E13}, // 溪 -> 渓
    {.from = 0x6EAB, .to = 0x6E29}, // 溫 -> 温
    {.from = 0x6EEF, .to = 0x6EDE}, // 滯 -> 滞
    {.from = 0x6EFF, .to = 0x6E80}, // 滿 -> 満
    {.from = 0x6F5B, .to = 0x6F5C}, // 潛 -> 潜
    {.from = 0x6F81, .to = 0x6E0B}, // 澁 -> 渋
    {.from = 0x6FA4, .to = 0x6CA2}, // 澤 -> 沢
    {.from = 0x6FD5, .to = 0x6E7F}, // 濕 -> 湿
    {.from = 0x6FDF, .to = 0x6E08}, // 濟 -> 済
    {.from = 0x6FF1, .to = 0x6D5C}, // 濱 -> 浜
    {.from = 0x7027, .to = 0x6EDD}, // 瀧 -> 滝
    {.from = 0x7028, .to = 0x702C}, // 瀨 -> 瀬
    {.from = 0x7063, .to = 0x6E7E}, // 灣 -> 湾
    {.from = 0x71C8, .to = 0x706F}, // 燈 -> 灯
    {.from = 0x71D2, .to = 0x713C}, // 燒 -> 焼
    {.from = 0x71DF, .to = 0x55B6}, // 營 -> 営
    {.from = 0x7210, .to = 0x7089}, // 爐 -> 炉
    {.from = 0x722D, .to = 0x4E89}, // 爭 -> 争
    {.from = 0x7232, .to = 0x70BA}, // 爲 -> 為
    {.from = 0x72A7, .to = 0x72A0}, // 犧 -> 犠
    {.from = 0x72C0, .to = 0x72B6}, // 狀 -> 状
    {.from = 0x72F9, .to = 0x72ED}, // 狹 -> 狭
    {.from = 0x7368, .to = 0x72EC}, // 獨 -> 独
    {.from = 0x7375, .to = 0x731F}, // 獵 -> 猟
    {.from = 0x7378, .to = 0x7363}, // 獸 -> 獣
    {.from = 0x737B, .to = 0x732E}, // 獻 -> 献
    {.from = 0x7464, .to = 0x7476}, // 瑤 -> 瑶
    {.from = 0x74E3, .to = 0x5F01}, // 瓣 -> 弁
    {.from = 0x7501, .to = 0x74F6}, // 甁 -> 瓶
    {.from = 0x7522, .to = 0x7523}, // 產 -> 産
    {.from = 0x756B, .to = 0x753B}, // 畫 -> 画
    {.from = 0x7576, .to = 0x5F53}, // 當 -> 当
    {.from = 0x758A, .to = 0x7573}, // 疊 -> 畳
    {.from = 0x7626, .to = 0x75E9}, // 瘦 -> 痩
    {.from = 0x7661, .to = 0x75F4}, // 癡 -> 痴
    {.from = 0x767C, .to = 0x767A}, // 發 -> 発
    {.from = 0x76DC, .to = 0x76D7}, // 盜 -> 盗
    {.from = 0x76E1, .to = 0x5C3D}, // 盡 -> 尽
    {.from = 0x771E, .to = 0x771F}, // 眞 -> 真
    {.from = 0x784F, .to = 0x7814}, // 硏 -> 研
    {.from = 0x788E, .to = 0x7815}, // 碎 -> 砕
    {.from = 0x7955, .to = 0x79D8}, // 祕 -> 秘
    {.from = 0x797F, .to = 0x7984}, // 祿 -> 禄
    {.from = 0x79AA, .to = 0x7985}, // 禪 -> 禅
    {.from = 0x79AE, .to = 0x793C}, // 禮 -> 礼
    {.from = 0x7A05, .to = 0x7A0E}, // 稅 -> 税
    {.from = 0x7A31, .to = 0x79F0}, // 稱 -> 称
    {.from = 0x7A3B, .to = 0x7A32}, // 稻 -> 稲
    {.from = 0x7A57, .to = 0x7A42}, // 穗 -> 穂
    {.from = 0x7A69, .to = 0x7A4F}, // 穩 -> 穏
    {.from = 0x7A70, .to = 0x7A63}, // 穰 -> 穣
    {.from = 0x7ACA, .to = 0x7A83}, // 竊 -> 窃
    {.from = 0x7ADD, .to = 0x4E26}, // 竝 -> 並
    {.from = 0x7CB9, .to = 0x7C8B}, // 粹 -> 粋
    {.from = 0x7D55, .to = 0x7D76}, // 絕 -> 絶
    {.from = 0x7D72, .to = 0x7CF8}, // 絲 -> 糸
    {.from = 0x7D93, .to = 0x7D4C}, // 經 -> 経
    {.from = 0x7DA0, .to = 0x7DD1}, // 綠 -> 緑
    {.from = 0x7DD6, .to = 0x7DD2}, // 緖 -> 緒
    {.from = 0x7DE3, .to = 0x7E01}, // 緣 -> 縁
    {.from = 0x7E23, .to = 0x770C}, // 縣 -> 県
    {.from = 0x7E31, .to = 0x7E26}, // 縱 -> 縦
    {.from = 0x7E3D, .to = 0x7DCF}, // 總 -> 総
    {.from = 0x7E69, .to = 0x7E04}, // 繩 -> 縄
    {.from = 0x7E6A, .to = 0x7D75}, // 繪 -> 絵
    {.from = 0x7E7C, .to = 0x7D99}, // 繼 -> 継
    {.from = 0x7E8C, .to = 0x7D9A}, // 續 -> 続
    {.from = 0x7E96, .to = 0x7E4A}, // 纖 -> 繊
    {.from = 0x7F3A, .to = 0x6B20}, // 缺 -> 欠
    {.from = 0x7F50, .to = 0x7F36}, // 罐 -> 缶
    {.from = 0x8070, .to = 0x8061}, // 聰 -> 聡
    {.from = 0x8072, .to = 0x58F0}, // 聲 -> 声
    {.from = 0x807D, .to = 0x8074}, // 聽 -> 聴
    {.from = 0x8085, .to = 0x7C9B}, // 肅 -> 粛
    {.from = 0x812B, .to = 0x8131}, // 脫 -> 脱
    {.from = 0x8166, .to = 0x8133}, // 腦 -> 脳
    {.from = 0x81BD, .to = 0x80C6}, // 膽 -> 胆
    {.from = 0x81DF, .to = 0x81D3}, // 臟 -> 臓
    {.from = 0x81FA, .to = 0x53F0}, // 臺 -> 台
    {.from = 0x8207, .to = 0x4E0E}, // 與 -> 与
    {.from = 0x820A, .to = 0x65E7}, // 舊 -> 旧
    {.from = 0x820D, .to = 0x820E}, // 舍 -> 舎
    {.from = 0x8216, .to = 0x8217}, // 舖 -> 舗
    {.from = 0x8277, .to = 0x8276}, // 艷 -> 艶
    {.from = 0x838A, .to = 0x8358}, // 莊 -> 荘
    {.from = 0x8396, .to = 0x830E}, // 莖 -> 茎
    {.from = 0x842C, .to = 0x4E07}, // 萬 -> 万
    {.from = 0x85B0, .to = 0x85AB}, // 薰 -> 薫
    {.from = 0x85CF, .to = 0x8535}, // 藏 -> 蔵
    {.from = 0x85DD, .to = 0x82B8}, // 藝 -> 芸
    {.from = 0x85E5, .to = 0x85AC}, // 藥 -> 薬
    {.from = 0x8655, .to = 0x51E6}, // 處 -> 処
    {.from = 0x865B, .to = 0x865A}, // 虛 -> 虚
    {.from = 0x865F, .to = 0x53F7}, // 號 -> 号
    {.from = 0x87A2, .to = 0x86CD}, // 螢 -> 蛍
    {.from = 0x87F2, .to = 0x866B}, // 蟲 -> 虫
    {.from = 0x8836, .to = 0x8695}, // 蠶 -> 蚕
    {.from = 0x883B, .to = 0x86EE}, // 蠻 -> 蛮
    {.from = 0x885E, .to = 0x885B}, // 衞 -> 衛
    {.from = 0x88DD, .to = 0x88C5}, // 裝 -> 装
    {.from = 0x8943, .to = 0x8912}, // 襃 -> 褒
    {.from = 0x89BA, .to = 0x899A}, // 覺 -> 覚
    {.from = 0x89BD, .to = 0x89A7}, // 覽 -> 覧
    {.from = 0x89C0, .to = 0x89B3}, // 觀 -> 観
    {.from = 0x89F8, .to = 0x89E6}, // 觸 -> 触
    {.from = 0x8B20, .to = 0x8B21}, // 謠 -> 謡
    {.from = 0x8B49, .to = 0x8A3C}, // 證 -> 証
    {.from = 0x8B6F, .to = 0x8A33}, // 譯 -> 訳
    {.from = 0x8B7D, .to = 0x8A89}, // 譽 -> 誉
    {.from = 0x8B80, .to = 0x8AAD}, // 讀 -> 読
    {.from = 0x8B8A, .to = 0x5909}, // 變 -> 変
    {.from = 0x8B93, .to = 0x8B72}, // 讓 -> 譲
    {.from = 0x8C50, .to = 0x8C4A}, // 豐 -> 豊
    {.from = 0x8C6B, .to = 0x4E88}, // 豫 -> 予
    {.from = 0x8CB3, .to = 0x5F10}, // 貳 -> 弐
    {.from = 0x8CE3, .to = 0x58F2}, // 賣 -> 売
    {.from = 0x8CF4, .to = 0x983C}, // 賴 -> 頼
    {.from = 0x8D0A, .to = 0x8CDB}, // 贊 -> 賛
    {.from = 0x8E10, .to = 0x8DF5}, // 踐 -> 践
    {.from = 0x8F15, .to = 0x8EFD}, // 輕 -> 軽
    {.from = 0x8F49, .to = 0x8EE2}, // 轉 -> 転
    {.from = 0x8FA8, .to = 0x5F01}, // 辨 -> 弁
    {.from = 0x8FAD, .to = 0x8F9E}, // 辭 -> 辞
    {.from = 0x8FAF, .to = 0x5F01}, // 辯 -> 弁
    {.from = 0x9059, .to = 0x9065}, // 遙 -> 遥
    {.from = 0x905E, .to = 0x9013}, // 遞 -> 逓
    {.from = 0x9072, .to = 0x9045}, // 遲 -> 遅
    {.from = 0x908A, .to = 0x8FBA}, // 邊 -> 辺
    {.from = 0x90DE, .to = 0x90CE}, // 郞 -> 郎
    {.from = 0x9115, .to = 0x90F7}, // 鄕 -> 郷
    {.from = 0x9189, .to = 0x9154}, // 醉 -> 酔
    {.from = 0x91AB, .to = 0x533B}, // 醫 -> 医
    {.from = 0x91C0, .to = 0x91B8}, // 釀 -> 醸
    {.from = 0x91CB, .to = 0x91C8}, // 釋 -> 釈
    {.from = 0x92B3, .to = 0x92ED}, // 銳 -> 鋭
    {.from = 0x9304, .to = 0x9332}, // 錄 -> 録
    {.from = 0x9322, .to = 0x92AD}, // 錢 -> 銭
    {.from = 0x934A, .to = 0x932C}, // 鍊 -> 錬
    {.from = 0x93AD, .to = 0x93AE}, // 鎭 -> 鎮
    {.from = 0x9435, .to = 0x9244}, // 鐵 -> 鉄
    {.from = 0x9444, .to = 0x92F3}, // 鑄 -> 鋳
    {.from = 0x945B, .to = 0x9271}, // 鑛 -> 鉱
    {.from = 0x95DC, .to = 0x95A2}, // 關 -> 関
    {.from = 0x9677, .to = 0x9665}, // 陷 -> 陥
    {.from = 0x96A8, .to = 0x968F}, // 隨 -> 随
    {.from = 0x96AA, .to = 0x967A}, // 險 -> 険
    {.from = 0x96B1, .to = 0x96A0}, // 隱 -> 隠
    {.from = 0x96B8, .to = 0x96B7}, // 隸 -> 隷
    {.from = 0x96D9, .to = 0x53CC}, // 雙 -> 双
    {.from = 0x96DC, .to = 0x96D1}, // 雜 -> 雑
    {.from = 0x9748, .to = 0x970A}, // 靈 -> 霊
    {.from = 0x9751, .to = 0x9752}, // 靑 -> 青
    {.from = 0x975C, .to = 0x9759}, // 靜 -> 静
    {.from = 0x984F, .to = 0x9854}, // 顏 -> 顔
    {.from = 0x986F, .to = 0x9855}, // 顯 -> 顕
    {.from = 0x98EE, .to = 0x98F2}, // 飮 -> 飲
    {.from = 0x9918, .to = 0x4F59}, // 餘 -> 余
    {.from = 0x9920, .to = 0x9905}, // 餠 -> 餅
    {.from = 0x9A37, .to = 0x9A12}, // 騷 -> 騒
    {.from = 0x9A45, .to = 0x99C6}, // 驅 -> 駆
    {.from = 0x9A57, .to = 0x9A13}, // 驗 -> 験
    {.from = 0x9A5B, .to = 0x99C5}, // 驛 -> 駅
    {.from = 0x9AD3, .to = 0x9AC4}, // 髓 -> 髄
    {.from = 0x9AD4, .to = 0x4F53}, // 體 -> 体
    {.from = 0x9AEE, .to = 0x9AEA}, // 髮 -> 髪
    {.from = 0x9B2A, .to = 0x95D8}, // 鬪 -> 闘
    {.from = 0x9DC4, .to = 0x9D8F}, // 鷄 -> 鶏
    {.from = 0x9E7D, .to = 0x5869}, // 鹽 -> 塩
    {.from = 0x9EA5, .to = 0x9EA6}, // 麥 -> 麦
    {.from = 0x9EB5, .to = 0x9EBA}, // 麵 -> 麺
    {.from = 0x9EC3, .to = 0x9EC4}, // 黃 -> 黄
    {.from = 0x9ED1, .to = 0x9ED2}, // 黑 -> 黒
    {.from = 0x9ED8, .to = 0x9ED9}, // 默 -> 黙
    {.from = 0x9EDE, .to = 0x70B9}, // 點 -> 点
    {.from = 0x9EE8, .to = 0x515A}, // 黨 -> 党
    {.from = 0x9F4A, .to = 0x6589}, // 齊 -> 斉
    {.from = 0x9F4B, .to = 0x658E}, // 齋 -> 斎
    {.from = 0x9F52, .to = 0x6B6F}, // 齒 -> 歯
    {.from = 0x9F61, .to = 0x9F62}, // 齡 -> 齢
    {.from = 0x9F8D, .to = 0x7ADC}, // 龍 -> 竜
    {.from = 0x9F9C, .to = 0x4E80}, // 龜 -> 亀
}};

// s_hiraganaToDakutenDict: the voiced counterpart the iteration mark U+309E adds.
inline constexpr std::array<CharPair, 21> kHiraganaToDakuten{{
    {.from = 0x3046, .to = 0x3094}, // う -> ゔ
    {.from = 0x304B, .to = 0x304C}, // か -> が
    {.from = 0x304D, .to = 0x304E}, // き -> ぎ
    {.from = 0x304F, .to = 0x3050}, // く -> ぐ
    {.from = 0x3051, .to = 0x3052}, // け -> げ
    {.from = 0x3053, .to = 0x3054}, // こ -> ご
    {.from = 0x3055, .to = 0x3056}, // さ -> ざ
    {.from = 0x3057, .to = 0x3058}, // し -> じ
    {.from = 0x3059, .to = 0x305A}, // す -> ず
    {.from = 0x305B, .to = 0x305C}, // せ -> ぜ
    {.from = 0x305D, .to = 0x305E}, // そ -> ぞ
    {.from = 0x305F, .to = 0x3060}, // た -> だ
    {.from = 0x3061, .to = 0x3062}, // ち -> ぢ
    {.from = 0x3064, .to = 0x3065}, // つ -> づ
    {.from = 0x3066, .to = 0x3067}, // て -> で
    {.from = 0x3068, .to = 0x3069}, // と -> ど
    {.from = 0x306F, .to = 0x3070}, // は -> ば
    {.from = 0x3072, .to = 0x3073}, // ひ -> び
    {.from = 0x3075, .to = 0x3076}, // ふ -> ぶ
    {.from = 0x3078, .to = 0x3079}, // へ -> べ
    {.from = 0x307B, .to = 0x307C}, // ほ -> ぼ
}};

// s_kanaFinalVowelDict: the vowel a kana ends on, for the elongation trigger test.
inline constexpr std::array<CharPair, 82> kKanaFinalVowel{{
    {.from = 0x3041, .to = 0x3042}, // ぁ -> あ
    {.from = 0x3042, .to = 0x3042}, // あ -> あ
    {.from = 0x3043, .to = 0x3044}, // ぃ -> い
    {.from = 0x3044, .to = 0x3044}, // い -> い
    {.from = 0x3045, .to = 0x3046}, // ぅ -> う
    {.from = 0x3046, .to = 0x3046}, // う -> う
    {.from = 0x3047, .to = 0x3048}, // ぇ -> え
    {.from = 0x3048, .to = 0x3048}, // え -> え
    {.from = 0x3049, .to = 0x304A}, // ぉ -> お
    {.from = 0x304A, .to = 0x304A}, // お -> お
    {.from = 0x304B, .to = 0x3042}, // か -> あ
    {.from = 0x304C, .to = 0x3042}, // が -> あ
    {.from = 0x304D, .to = 0x3044}, // き -> い
    {.from = 0x304E, .to = 0x3044}, // ぎ -> い
    {.from = 0x304F, .to = 0x3046}, // く -> う
    {.from = 0x3050, .to = 0x3046}, // ぐ -> う
    {.from = 0x3051, .to = 0x3048}, // け -> え
    {.from = 0x3052, .to = 0x3048}, // げ -> え
    {.from = 0x3053, .to = 0x304A}, // こ -> お
    {.from = 0x3054, .to = 0x304A}, // ご -> お
    {.from = 0x3055, .to = 0x3042}, // さ -> あ
    {.from = 0x3056, .to = 0x3042}, // ざ -> あ
    {.from = 0x3057, .to = 0x3044}, // し -> い
    {.from = 0x3058, .to = 0x3044}, // じ -> い
    {.from = 0x3059, .to = 0x3046}, // す -> う
    {.from = 0x305A, .to = 0x3046}, // ず -> う
    {.from = 0x305B, .to = 0x3048}, // せ -> え
    {.from = 0x305C, .to = 0x3048}, // ぜ -> え
    {.from = 0x305D, .to = 0x304A}, // そ -> お
    {.from = 0x305E, .to = 0x304A}, // ぞ -> お
    {.from = 0x305F, .to = 0x3042}, // た -> あ
    {.from = 0x3060, .to = 0x3042}, // だ -> あ
    {.from = 0x3061, .to = 0x3044}, // ち -> い
    {.from = 0x3062, .to = 0x3044}, // ぢ -> い
    {.from = 0x3064, .to = 0x3046}, // つ -> う
    {.from = 0x3065, .to = 0x3046}, // づ -> う
    {.from = 0x3066, .to = 0x3048}, // て -> え
    {.from = 0x3067, .to = 0x3048}, // で -> え
    {.from = 0x3068, .to = 0x304A}, // と -> お
    {.from = 0x3069, .to = 0x304A}, // ど -> お
    {.from = 0x306A, .to = 0x3042}, // な -> あ
    {.from = 0x306B, .to = 0x3044}, // に -> い
    {.from = 0x306C, .to = 0x3046}, // ぬ -> う
    {.from = 0x306D, .to = 0x3048}, // ね -> え
    {.from = 0x306E, .to = 0x304A}, // の -> お
    {.from = 0x306F, .to = 0x3042}, // は -> あ
    {.from = 0x3070, .to = 0x3042}, // ば -> あ
    {.from = 0x3071, .to = 0x3042}, // ぱ -> あ
    {.from = 0x3072, .to = 0x3044}, // ひ -> い
    {.from = 0x3073, .to = 0x3044}, // び -> い
    {.from = 0x3074, .to = 0x3044}, // ぴ -> い
    {.from = 0x3075, .to = 0x3046}, // ふ -> う
    {.from = 0x3076, .to = 0x3046}, // ぶ -> う
    {.from = 0x3077, .to = 0x3046}, // ぷ -> う
    {.from = 0x3078, .to = 0x3048}, // へ -> え
    {.from = 0x3079, .to = 0x3048}, // べ -> え
    {.from = 0x307A, .to = 0x3048}, // ぺ -> え
    {.from = 0x307B, .to = 0x304A}, // ほ -> お
    {.from = 0x307C, .to = 0x304A}, // ぼ -> お
    {.from = 0x307D, .to = 0x304A}, // ぽ -> お
    {.from = 0x307E, .to = 0x3042}, // ま -> あ
    {.from = 0x307F, .to = 0x3044}, // み -> い
    {.from = 0x3080, .to = 0x3046}, // む -> う
    {.from = 0x3081, .to = 0x3048}, // め -> え
    {.from = 0x3082, .to = 0x304A}, // も -> お
    {.from = 0x3083, .to = 0x3042}, // ゃ -> あ
    {.from = 0x3084, .to = 0x3042}, // や -> あ
    {.from = 0x3085, .to = 0x3046}, // ゅ -> う
    {.from = 0x3086, .to = 0x3046}, // ゆ -> う
    {.from = 0x3087, .to = 0x304A}, // ょ -> お
    {.from = 0x3088, .to = 0x304A}, // よ -> お
    {.from = 0x3089, .to = 0x3042}, // ら -> あ
    {.from = 0x308A, .to = 0x3044}, // り -> い
    {.from = 0x308B, .to = 0x3046}, // る -> う
    {.from = 0x308C, .to = 0x3048}, // れ -> え
    {.from = 0x308D, .to = 0x304A}, // ろ -> お
    {.from = 0x308E, .to = 0x3042}, // ゎ -> あ
    {.from = 0x308F, .to = 0x3042}, // わ -> あ
    {.from = 0x3092, .to = 0x304A}, // を -> お
    {.from = 0x3094, .to = 0x3046}, // ゔ -> う
    {.from = 0x3095, .to = 0x3042}, // ゕ -> あ
    {.from = 0x3096, .to = 0x3048}, // ゖ -> え
}};

// SmallVowelHiraganaToFinalVowelDict: the vowel a small hiragana vowel stands for.
inline constexpr std::array<CharPair, 5> kSmallVowelHiraganaToFinalVowel{{
    {.from = 0x3041, .to = 0x3042}, // ぁ -> あ
    {.from = 0x3043, .to = 0x3044}, // ぃ -> い
    {.from = 0x3045, .to = 0x3046}, // ぅ -> う
    {.from = 0x3047, .to = 0x3048}, // ぇ -> え
    {.from = 0x3049, .to = 0x304A}, // ぉ -> お
}};

// s_leftToRightBracketDict: opening bracket to its closing counterpart.
inline constexpr std::array<CharPair, 28> kLeftToRightBracket{{
    {.from = 0x0028, .to = 0x0029}, // ( -> )
    {.from = 0x005B, .to = 0x005D}, // [ -> ]
    {.from = 0x007B, .to = 0x007D}, // { -> }
    {.from = 0x27E8, .to = 0x27E9}, // ⟨ -> ⟩
    {.from = 0x3008, .to = 0x3009}, // 〈 -> 〉
    {.from = 0x300A, .to = 0x300B}, // 《 -> 》
    {.from = 0x300C, .to = 0x300D}, // 「 -> 」
    {.from = 0x300E, .to = 0x300F}, // 『 -> 』
    {.from = 0x3010, .to = 0x3011}, // 【 -> 】
    {.from = 0x3014, .to = 0x3015}, // 〔 -> 〕
    {.from = 0x301D, .to = 0x301F}, // 〝 -> 〟
    {.from = 0xFE17, .to = 0xFE18}, // ︗ -> ︘
    {.from = 0xFE35, .to = 0xFE36}, // ︵ -> ︶
    {.from = 0xFE37, .to = 0xFE38}, // ︷ -> ︸
    {.from = 0xFE39, .to = 0xFE3A}, // ︹ -> ︺
    {.from = 0xFE3B, .to = 0xFE3C}, // ︻ -> ︼
    {.from = 0xFE3D, .to = 0xFE3E}, // ︽ -> ︾
    {.from = 0xFE3F, .to = 0xFE40}, // ︿ -> ﹀
    {.from = 0xFE41, .to = 0xFE42}, // ﹁ -> ﹂
    {.from = 0xFE43, .to = 0xFE44}, // ﹃ -> ﹄
    {.from = 0xFE47, .to = 0xFE48}, // ﹇ -> ﹈
    {.from = 0xFF02, .to = 0xFF02}, // ＂ -> ＂
    {.from = 0xFF07, .to = 0xFF07}, // ＇ -> ＇
    {.from = 0xFF08, .to = 0xFF09}, // （ -> ）
    {.from = 0xFF1C, .to = 0xFF1E}, // ＜ -> ＞
    {.from = 0xFF3B, .to = 0xFF3D}, // ［ -> ］
    {.from = 0xFF5B, .to = 0xFF5D}, // ｛ -> ｝
    {.from = 0xFF62, .to = 0xFF63}, // ｢ -> ｣
}};

// Supplementary-plane single-code-point mappings: kyuujitai and the hentaigana block
// U+1B002-U+1B11E. Keyed by code point, so the caller decodes the surrogate pair first.
inline constexpr std::array<SupplementaryPair, 288> kSupplementaryNormalization{{
    {.from = 0x1B001, .to = 0x3048}, // 𛀁 -> え
    {.from = 0x1B002, .to = 0x3042}, // 𛀂 -> あ
    {.from = 0x1B003, .to = 0x3042}, // 𛀃 -> あ
    {.from = 0x1B004, .to = 0x3042}, // 𛀄 -> あ
    {.from = 0x1B005, .to = 0x3042}, // 𛀅 -> あ
    {.from = 0x1B006, .to = 0x3044}, // 𛀆 -> い
    {.from = 0x1B007, .to = 0x3044}, // 𛀇 -> い
    {.from = 0x1B008, .to = 0x3044}, // 𛀈 -> い
    {.from = 0x1B009, .to = 0x3044}, // 𛀉 -> い
    {.from = 0x1B00A, .to = 0x3046}, // 𛀊 -> う
    {.from = 0x1B00B, .to = 0x3046}, // 𛀋 -> う
    {.from = 0x1B00C, .to = 0x3046}, // 𛀌 -> う
    {.from = 0x1B00D, .to = 0x3046}, // 𛀍 -> う
    {.from = 0x1B00E, .to = 0x3046}, // 𛀎 -> う
    {.from = 0x1B00F, .to = 0x3048}, // 𛀏 -> え
    {.from = 0x1B010, .to = 0x3048}, // 𛀐 -> え
    {.from = 0x1B011, .to = 0x3048}, // 𛀑 -> え
    {.from = 0x1B012, .to = 0x3048}, // 𛀒 -> え
    {.from = 0x1B013, .to = 0x3048}, // 𛀓 -> え
    {.from = 0x1B014, .to = 0x304A}, // 𛀔 -> お
    {.from = 0x1B015, .to = 0x304A}, // 𛀕 -> お
    {.from = 0x1B016, .to = 0x304A}, // 𛀖 -> お
    {.from = 0x1B017, .to = 0x304B}, // 𛀗 -> か
    {.from = 0x1B018, .to = 0x304B}, // 𛀘 -> か
    {.from = 0x1B019, .to = 0x304B}, // 𛀙 -> か
    {.from = 0x1B01A, .to = 0x304B}, // 𛀚 -> か
    {.from = 0x1B01B, .to = 0x304B}, // 𛀛 -> か
    {.from = 0x1B01C, .to = 0x304B}, // 𛀜 -> か
    {.from = 0x1B01D, .to = 0x304B}, // 𛀝 -> か
    {.from = 0x1B01E, .to = 0x304B}, // 𛀞 -> か
    {.from = 0x1B01F, .to = 0x304B}, // 𛀟 -> か
    {.from = 0x1B020, .to = 0x304B}, // 𛀠 -> か
    {.from = 0x1B021, .to = 0x304B}, // 𛀡 -> か
    {.from = 0x1B022, .to = 0x304B}, // 𛀢 -> か
    {.from = 0x1B023, .to = 0x304D}, // 𛀣 -> き
    {.from = 0x1B024, .to = 0x304D}, // 𛀤 -> き
    {.from = 0x1B025, .to = 0x304D}, // 𛀥 -> き
    {.from = 0x1B026, .to = 0x304D}, // 𛀦 -> き
    {.from = 0x1B027, .to = 0x304D}, // 𛀧 -> き
    {.from = 0x1B028, .to = 0x304D}, // 𛀨 -> き
    {.from = 0x1B029, .to = 0x304D}, // 𛀩 -> き
    {.from = 0x1B02A, .to = 0x304D}, // 𛀪 -> き
    {.from = 0x1B02B, .to = 0x304F}, // 𛀫 -> く
    {.from = 0x1B02C, .to = 0x304F}, // 𛀬 -> く
    {.from = 0x1B02D, .to = 0x304F}, // 𛀭 -> く
    {.from = 0x1B02E, .to = 0x304F}, // 𛀮 -> く
    {.from = 0x1B02F, .to = 0x304F}, // 𛀯 -> く
    {.from = 0x1B030, .to = 0x304F}, // 𛀰 -> く
    {.from = 0x1B031, .to = 0x304F}, // 𛀱 -> く
    {.from = 0x1B032, .to = 0x3051}, // 𛀲 -> け
    {.from = 0x1B033, .to = 0x3051}, // 𛀳 -> け
    {.from = 0x1B034, .to = 0x3051}, // 𛀴 -> け
    {.from = 0x1B035, .to = 0x3051}, // 𛀵 -> け
    {.from = 0x1B036, .to = 0x3051}, // 𛀶 -> け
    {.from = 0x1B037, .to = 0x3051}, // 𛀷 -> け
    {.from = 0x1B038, .to = 0x3053}, // 𛀸 -> こ
    {.from = 0x1B039, .to = 0x3053}, // 𛀹 -> こ
    {.from = 0x1B03A, .to = 0x3053}, // 𛀺 -> こ
    {.from = 0x1B03B, .to = 0x3053}, // 𛀻 -> こ
    {.from = 0x1B03C, .to = 0x3055}, // 𛀼 -> さ
    {.from = 0x1B03D, .to = 0x3055}, // 𛀽 -> さ
    {.from = 0x1B03E, .to = 0x3055}, // 𛀾 -> さ
    {.from = 0x1B03F, .to = 0x3055}, // 𛀿 -> さ
    {.from = 0x1B040, .to = 0x3055}, // 𛁀 -> さ
    {.from = 0x1B041, .to = 0x3055}, // 𛁁 -> さ
    {.from = 0x1B042, .to = 0x3055}, // 𛁂 -> さ
    {.from = 0x1B043, .to = 0x3055}, // 𛁃 -> さ
    {.from = 0x1B044, .to = 0x3057}, // 𛁄 -> し
    {.from = 0x1B045, .to = 0x3057}, // 𛁅 -> し
    {.from = 0x1B046, .to = 0x3057}, // 𛁆 -> し
    {.from = 0x1B047, .to = 0x3057}, // 𛁇 -> し
    {.from = 0x1B048, .to = 0x3057}, // 𛁈 -> し
    {.from = 0x1B049, .to = 0x3057}, // 𛁉 -> し
    {.from = 0x1B04A, .to = 0x3059}, // 𛁊 -> す
    {.from = 0x1B04B, .to = 0x3059}, // 𛁋 -> す
    {.from = 0x1B04C, .to = 0x3059}, // 𛁌 -> す
    {.from = 0x1B04D, .to = 0x3059}, // 𛁍 -> す
    {.from = 0x1B04E, .to = 0x3059}, // 𛁎 -> す
    {.from = 0x1B04F, .to = 0x3059}, // 𛁏 -> す
    {.from = 0x1B050, .to = 0x3059}, // 𛁐 -> す
    {.from = 0x1B051, .to = 0x3059}, // 𛁑 -> す
    {.from = 0x1B052, .to = 0x305B}, // 𛁒 -> せ
    {.from = 0x1B053, .to = 0x305B}, // 𛁓 -> せ
    {.from = 0x1B054, .to = 0x305B}, // 𛁔 -> せ
    {.from = 0x1B055, .to = 0x305B}, // 𛁕 -> せ
    {.from = 0x1B056, .to = 0x305B}, // 𛁖 -> せ
    {.from = 0x1B057, .to = 0x305D}, // 𛁗 -> そ
    {.from = 0x1B058, .to = 0x305D}, // 𛁘 -> そ
    {.from = 0x1B059, .to = 0x305D}, // 𛁙 -> そ
    {.from = 0x1B05A, .to = 0x305D}, // 𛁚 -> そ
    {.from = 0x1B05B, .to = 0x305D}, // 𛁛 -> そ
    {.from = 0x1B05C, .to = 0x305D}, // 𛁜 -> そ
    {.from = 0x1B05D, .to = 0x305D}, // 𛁝 -> そ
    {.from = 0x1B05E, .to = 0x305F}, // 𛁞 -> た
    {.from = 0x1B05F, .to = 0x305F}, // 𛁟 -> た
    {.from = 0x1B060, .to = 0x305F}, // 𛁠 -> た
    {.from = 0x1B061, .to = 0x305F}, // 𛁡 -> た
    {.from = 0x1B062, .to = 0x3061}, // 𛁢 -> ち
    {.from = 0x1B063, .to = 0x3061}, // 𛁣 -> ち
    {.from = 0x1B064, .to = 0x3061}, // 𛁤 -> ち
    {.from = 0x1B065, .to = 0x3061}, // 𛁥 -> ち
    {.from = 0x1B066, .to = 0x3061}, // 𛁦 -> ち
    {.from = 0x1B067, .to = 0x3061}, // 𛁧 -> ち
    {.from = 0x1B068, .to = 0x3061}, // 𛁨 -> ち
    {.from = 0x1B069, .to = 0x3064}, // 𛁩 -> つ
    {.from = 0x1B06A, .to = 0x3064}, // 𛁪 -> つ
    {.from = 0x1B06B, .to = 0x3064}, // 𛁫 -> つ
    {.from = 0x1B06C, .to = 0x3064}, // 𛁬 -> つ
    {.from = 0x1B06D, .to = 0x3064}, // 𛁭 -> つ
    {.from = 0x1B06E, .to = 0x3066}, // 𛁮 -> て
    {.from = 0x1B06F, .to = 0x3066}, // 𛁯 -> て
    {.from = 0x1B070, .to = 0x3066}, // 𛁰 -> て
    {.from = 0x1B071, .to = 0x3066}, // 𛁱 -> て
    {.from = 0x1B072, .to = 0x3066}, // 𛁲 -> て
    {.from = 0x1B073, .to = 0x3066}, // 𛁳 -> て
    {.from = 0x1B074, .to = 0x3066}, // 𛁴 -> て
    {.from = 0x1B075, .to = 0x3066}, // 𛁵 -> て
    {.from = 0x1B076, .to = 0x3066}, // 𛁶 -> て
    {.from = 0x1B077, .to = 0x3068}, // 𛁷 -> と
    {.from = 0x1B078, .to = 0x3068}, // 𛁸 -> と
    {.from = 0x1B079, .to = 0x3068}, // 𛁹 -> と
    {.from = 0x1B07A, .to = 0x3068}, // 𛁺 -> と
    {.from = 0x1B07B, .to = 0x3068}, // 𛁻 -> と
    {.from = 0x1B07C, .to = 0x3068}, // 𛁼 -> と
    {.from = 0x1B07D, .to = 0x3068}, // 𛁽 -> と
    {.from = 0x1B07E, .to = 0x306A}, // 𛁾 -> な
    {.from = 0x1B07F, .to = 0x306A}, // 𛁿 -> な
    {.from = 0x1B080, .to = 0x306A}, // 𛂀 -> な
    {.from = 0x1B081, .to = 0x306A}, // 𛂁 -> な
    {.from = 0x1B082, .to = 0x306A}, // 𛂂 -> な
    {.from = 0x1B083, .to = 0x306A}, // 𛂃 -> な
    {.from = 0x1B084, .to = 0x306A}, // 𛂄 -> な
    {.from = 0x1B085, .to = 0x306A}, // 𛂅 -> な
    {.from = 0x1B086, .to = 0x306A}, // 𛂆 -> な
    {.from = 0x1B087, .to = 0x306B}, // 𛂇 -> に
    {.from = 0x1B088, .to = 0x306B}, // 𛂈 -> に
    {.from = 0x1B089, .to = 0x306B}, // 𛂉 -> に
    {.from = 0x1B08A, .to = 0x306B}, // 𛂊 -> に
    {.from = 0x1B08B, .to = 0x306B}, // 𛂋 -> に
    {.from = 0x1B08C, .to = 0x306B}, // 𛂌 -> に
    {.from = 0x1B08D, .to = 0x306B}, // 𛂍 -> に
    {.from = 0x1B08E, .to = 0x306B}, // 𛂎 -> に
    {.from = 0x1B08F, .to = 0x306C}, // 𛂏 -> ぬ
    {.from = 0x1B090, .to = 0x306C}, // 𛂐 -> ぬ
    {.from = 0x1B091, .to = 0x306C}, // 𛂑 -> ぬ
    {.from = 0x1B092, .to = 0x306D}, // 𛂒 -> ね
    {.from = 0x1B093, .to = 0x306D}, // 𛂓 -> ね
    {.from = 0x1B094, .to = 0x306D}, // 𛂔 -> ね
    {.from = 0x1B095, .to = 0x306D}, // 𛂕 -> ね
    {.from = 0x1B096, .to = 0x306D}, // 𛂖 -> ね
    {.from = 0x1B097, .to = 0x306D}, // 𛂗 -> ね
    {.from = 0x1B098, .to = 0x306D}, // 𛂘 -> ね
    {.from = 0x1B099, .to = 0x306E}, // 𛂙 -> の
    {.from = 0x1B09A, .to = 0x306E}, // 𛂚 -> の
    {.from = 0x1B09B, .to = 0x306E}, // 𛂛 -> の
    {.from = 0x1B09C, .to = 0x306E}, // 𛂜 -> の
    {.from = 0x1B09D, .to = 0x306E}, // 𛂝 -> の
    {.from = 0x1B09E, .to = 0x306E}, // 𛂞 -> の
    {.from = 0x1B09F, .to = 0x306F}, // 𛂟 -> は
    {.from = 0x1B0A0, .to = 0x306F}, // 𛂠 -> は
    {.from = 0x1B0A1, .to = 0x306F}, // 𛂡 -> は
    {.from = 0x1B0A2, .to = 0x306F}, // 𛂢 -> は
    {.from = 0x1B0A3, .to = 0x306F}, // 𛂣 -> は
    {.from = 0x1B0A4, .to = 0x306F}, // 𛂤 -> は
    {.from = 0x1B0A5, .to = 0x306F}, // 𛂥 -> は
    {.from = 0x1B0A6, .to = 0x306F}, // 𛂦 -> は
    {.from = 0x1B0A7, .to = 0x306F}, // 𛂧 -> は
    {.from = 0x1B0A8, .to = 0x306F}, // 𛂨 -> は
    {.from = 0x1B0A9, .to = 0x3072}, // 𛂩 -> ひ
    {.from = 0x1B0AA, .to = 0x3072}, // 𛂪 -> ひ
    {.from = 0x1B0AB, .to = 0x3072}, // 𛂫 -> ひ
    {.from = 0x1B0AC, .to = 0x3072}, // 𛂬 -> ひ
    {.from = 0x1B0AD, .to = 0x3072}, // 𛂭 -> ひ
    {.from = 0x1B0AE, .to = 0x3072}, // 𛂮 -> ひ
    {.from = 0x1B0AF, .to = 0x3072}, // 𛂯 -> ひ
    {.from = 0x1B0B0, .to = 0x3075}, // 𛂰 -> ふ
    {.from = 0x1B0B1, .to = 0x3075}, // 𛂱 -> ふ
    {.from = 0x1B0B2, .to = 0x3075}, // 𛂲 -> ふ
    {.from = 0x1B0B3, .to = 0x3078}, // 𛂳 -> へ
    {.from = 0x1B0B4, .to = 0x3078}, // 𛂴 -> へ
    {.from = 0x1B0B5, .to = 0x3078}, // 𛂵 -> へ
    {.from = 0x1B0B6, .to = 0x3078}, // 𛂶 -> へ
    {.from = 0x1B0B7, .to = 0x3078}, // 𛂷 -> へ
    {.from = 0x1B0B8, .to = 0x3078}, // 𛂸 -> へ
    {.from = 0x1B0B9, .to = 0x3078}, // 𛂹 -> へ
    {.from = 0x1B0BA, .to = 0x307B}, // 𛂺 -> ほ
    {.from = 0x1B0BB, .to = 0x307B}, // 𛂻 -> ほ
    {.from = 0x1B0BC, .to = 0x307B}, // 𛂼 -> ほ
    {.from = 0x1B0BD, .to = 0x307B}, // 𛂽 -> ほ
    {.from = 0x1B0BE, .to = 0x307B}, // 𛂾 -> ほ
    {.from = 0x1B0BF, .to = 0x307B}, // 𛂿 -> ほ
    {.from = 0x1B0C0, .to = 0x307B}, // 𛃀 -> ほ
    {.from = 0x1B0C1, .to = 0x307B}, // 𛃁 -> ほ
    {.from = 0x1B0C2, .to = 0x307E}, // 𛃂 -> ま
    {.from = 0x1B0C3, .to = 0x307E}, // 𛃃 -> ま
    {.from = 0x1B0C4, .to = 0x307E}, // 𛃄 -> ま
    {.from = 0x1B0C5, .to = 0x307E}, // 𛃅 -> ま
    {.from = 0x1B0C6, .to = 0x307E}, // 𛃆 -> ま
    {.from = 0x1B0C7, .to = 0x307E}, // 𛃇 -> ま
    {.from = 0x1B0C8, .to = 0x307E}, // 𛃈 -> ま
    {.from = 0x1B0C9, .to = 0x307F}, // 𛃉 -> み
    {.from = 0x1B0CA, .to = 0x307F}, // 𛃊 -> み
    {.from = 0x1B0CB, .to = 0x307F}, // 𛃋 -> み
    {.from = 0x1B0CC, .to = 0x307F}, // 𛃌 -> み
    {.from = 0x1B0CD, .to = 0x307F}, // 𛃍 -> み
    {.from = 0x1B0CE, .to = 0x307F}, // 𛃎 -> み
    {.from = 0x1B0CF, .to = 0x307F}, // 𛃏 -> み
    {.from = 0x1B0D0, .to = 0x3080}, // 𛃐 -> む
    {.from = 0x1B0D1, .to = 0x3080}, // 𛃑 -> む
    {.from = 0x1B0D2, .to = 0x3080}, // 𛃒 -> む
    {.from = 0x1B0D3, .to = 0x3080}, // 𛃓 -> む
    {.from = 0x1B0D4, .to = 0x3081}, // 𛃔 -> め
    {.from = 0x1B0D5, .to = 0x3081}, // 𛃕 -> め
    {.from = 0x1B0D6, .to = 0x3081}, // 𛃖 -> め
    {.from = 0x1B0D7, .to = 0x3082}, // 𛃗 -> も
    {.from = 0x1B0D8, .to = 0x3082}, // 𛃘 -> も
    {.from = 0x1B0D9, .to = 0x3082}, // 𛃙 -> も
    {.from = 0x1B0DA, .to = 0x3082}, // 𛃚 -> も
    {.from = 0x1B0DB, .to = 0x3082}, // 𛃛 -> も
    {.from = 0x1B0DC, .to = 0x3082}, // 𛃜 -> も
    {.from = 0x1B0DD, .to = 0x3084}, // 𛃝 -> や
    {.from = 0x1B0DE, .to = 0x3084}, // 𛃞 -> や
    {.from = 0x1B0DF, .to = 0x3084}, // 𛃟 -> や
    {.from = 0x1B0E0, .to = 0x3084}, // 𛃠 -> や
    {.from = 0x1B0E1, .to = 0x3084}, // 𛃡 -> や
    {.from = 0x1B0E2, .to = 0x3084}, // 𛃢 -> や
    {.from = 0x1B0E3, .to = 0x3086}, // 𛃣 -> ゆ
    {.from = 0x1B0E4, .to = 0x3086}, // 𛃤 -> ゆ
    {.from = 0x1B0E5, .to = 0x3086}, // 𛃥 -> ゆ
    {.from = 0x1B0E6, .to = 0x3086}, // 𛃦 -> ゆ
    {.from = 0x1B0E7, .to = 0x3088}, // 𛃧 -> よ
    {.from = 0x1B0E8, .to = 0x3088}, // 𛃨 -> よ
    {.from = 0x1B0E9, .to = 0x3088}, // 𛃩 -> よ
    {.from = 0x1B0EA, .to = 0x3088}, // 𛃪 -> よ
    {.from = 0x1B0EB, .to = 0x3088}, // 𛃫 -> よ
    {.from = 0x1B0EC, .to = 0x3088}, // 𛃬 -> よ
    {.from = 0x1B0ED, .to = 0x3089}, // 𛃭 -> ら
    {.from = 0x1B0EE, .to = 0x3089}, // 𛃮 -> ら
    {.from = 0x1B0EF, .to = 0x3089}, // 𛃯 -> ら
    {.from = 0x1B0F0, .to = 0x3089}, // 𛃰 -> ら
    {.from = 0x1B0F1, .to = 0x308A}, // 𛃱 -> り
    {.from = 0x1B0F2, .to = 0x308A}, // 𛃲 -> り
    {.from = 0x1B0F3, .to = 0x308A}, // 𛃳 -> り
    {.from = 0x1B0F4, .to = 0x308A}, // 𛃴 -> り
    {.from = 0x1B0F5, .to = 0x308A}, // 𛃵 -> り
    {.from = 0x1B0F6, .to = 0x308A}, // 𛃶 -> り
    {.from = 0x1B0F7, .to = 0x308A}, // 𛃷 -> り
    {.from = 0x1B0F8, .to = 0x308B}, // 𛃸 -> る
    {.from = 0x1B0F9, .to = 0x308B}, // 𛃹 -> る
    {.from = 0x1B0FA, .to = 0x308B}, // 𛃺 -> る
    {.from = 0x1B0FB, .to = 0x308B}, // 𛃻 -> る
    {.from = 0x1B0FC, .to = 0x308B}, // 𛃼 -> る
    {.from = 0x1B0FD, .to = 0x308B}, // 𛃽 -> る
    {.from = 0x1B0FE, .to = 0x308C}, // 𛃾 -> れ
    {.from = 0x1B0FF, .to = 0x308C}, // 𛃿 -> れ
    {.from = 0x1B100, .to = 0x308C}, // 𛄀 -> れ
    {.from = 0x1B101, .to = 0x308C}, // 𛄁 -> れ
    {.from = 0x1B102, .to = 0x308D}, // 𛄂 -> ろ
    {.from = 0x1B103, .to = 0x308D}, // 𛄃 -> ろ
    {.from = 0x1B104, .to = 0x308D}, // 𛄄 -> ろ
    {.from = 0x1B105, .to = 0x308D}, // 𛄅 -> ろ
    {.from = 0x1B106, .to = 0x308D}, // 𛄆 -> ろ
    {.from = 0x1B107, .to = 0x308D}, // 𛄇 -> ろ
    {.from = 0x1B108, .to = 0x308F}, // 𛄈 -> わ
    {.from = 0x1B109, .to = 0x308F}, // 𛄉 -> わ
    {.from = 0x1B10A, .to = 0x308F}, // 𛄊 -> わ
    {.from = 0x1B10B, .to = 0x308F}, // 𛄋 -> わ
    {.from = 0x1B10C, .to = 0x308F}, // 𛄌 -> わ
    {.from = 0x1B10D, .to = 0x3044}, // 𛄍 -> い
    {.from = 0x1B10E, .to = 0x3044}, // 𛄎 -> い
    {.from = 0x1B10F, .to = 0x3044}, // 𛄏 -> い
    {.from = 0x1B110, .to = 0x3044}, // 𛄐 -> い
    {.from = 0x1B111, .to = 0x3044}, // 𛄑 -> い
    {.from = 0x1B112, .to = 0x3048}, // 𛄒 -> え
    {.from = 0x1B113, .to = 0x3048}, // 𛄓 -> え
    {.from = 0x1B114, .to = 0x3048}, // 𛄔 -> え
    {.from = 0x1B115, .to = 0x3048}, // 𛄕 -> え
    {.from = 0x1B116, .to = 0x3092}, // 𛄖 -> を
    {.from = 0x1B117, .to = 0x3092}, // 𛄗 -> を
    {.from = 0x1B118, .to = 0x3092}, // 𛄘 -> を
    {.from = 0x1B119, .to = 0x3092}, // 𛄙 -> を
    {.from = 0x1B11A, .to = 0x3092}, // 𛄚 -> を
    {.from = 0x1B11B, .to = 0x3092}, // 𛄛 -> を
    {.from = 0x1B11C, .to = 0x3092}, // 𛄜 -> を
    {.from = 0x1B11D, .to = 0x3093}, // 𛄝 -> ん
    {.from = 0x1B11E, .to = 0x3093}, // 𛄞 -> ん
    {.from = 0x26936, .to = 0x81F4}, // 𦤶 -> 致
    {.from = 0x2F862, .to = 0x59EB}, // 姬 -> 姫
}};

// s_charsToStrip: dropped when neither the first nor the last code unit.
inline constexpr std::array<char16_t, 12> kCharsToStrip{
    {0x0020, 0x002E, 0x003D, 0x00B7, 0x2020, 0x2021, 0x2605, 0x2606, 0x2661, 0x2665, 0x30A0, 0x30FB}};

// s_fuseji: censoring marks, all folded to U+25CB.
inline constexpr std::array<char16_t, 19> kFuseji{{0x0023,
                                                   0x002A,
                                                   0x00D7,
                                                   0x203B,
                                                   0x25A0,
                                                   0x25A1,
                                                   0x25B2,
                                                   0x25B3,
                                                   0x25BC,
                                                   0x25BD,
                                                   0x25C6,
                                                   0x25C7,
                                                   0x25C9,
                                                   0x25CB,
                                                   0x25CE,
                                                   0x25CF,
                                                   0x25EF,
                                                   0x2B24,
                                                   0x3007}};

// s_longVowelMarkChars.
inline constexpr std::array<char16_t, 3> kLongVowelMarks{{0x007E, 0x301C, 0x30FC}};

// s_sentenceTerminatingCharacters.
inline constexpr std::array<char16_t, 12> kSentenceTerminators{
    {0x000A, 0x001E, 0x0021, 0x003F, 0x2025, 0x2026, 0x3002, 0xFE12, 0xFE19, 0xFE30, 0xFF01, 0xFF1F}};

// SmallCombiningKanaSet: glued onto the preceding character by combinedForm().
inline constexpr std::array<char16_t, 18> kSmallCombiningKana{{0x3041,
                                                               0x3043,
                                                               0x3045,
                                                               0x3047,
                                                               0x3049,
                                                               0x3083,
                                                               0x3085,
                                                               0x3087,
                                                               0x308E,
                                                               0x30A1,
                                                               0x30A3,
                                                               0x30A5,
                                                               0x30A7,
                                                               0x30A9,
                                                               0x30E3,
                                                               0x30E5,
                                                               0x30E7,
                                                               0x30EE}};

// s_charactersToNormalize: the fast-bail set of normalizeText(). A text holding
// none of these is returned unchanged after the NFKC and uppercase passes.
inline constexpr std::array<char16_t, 592> kCharactersToNormalize{
    {0x0020, 0x0023, 0x002A, 0x002E, 0x003D, 0x00B7, 0x00D7, 0x2020, 0x2021, 0x203B, 0x25A0, 0x25A1, 0x25B2, 0x25B3,
     0x25BC, 0x25BD, 0x25C6, 0x25C7, 0x25C9, 0x25CB, 0x25CE, 0x25CF, 0x25EF, 0x2605, 0x2606, 0x2661, 0x2665, 0x2B24,
     0x2E81, 0x2E82, 0x2E83, 0x2E84, 0x2E85, 0x2E86, 0x2E87, 0x2E88, 0x2E89, 0x2E8A, 0x2E8B, 0x2E8C, 0x2E8D, 0x2E8E,
     0x2E8F, 0x2E90, 0x2E91, 0x2E92, 0x2E93, 0x2E94, 0x2E95, 0x2E96, 0x2E97, 0x2E98, 0x2E99, 0x2E9B, 0x2E9C, 0x2E9D,
     0x2E9E, 0x2EA0, 0x2EA1, 0x2EA2, 0x2EA3, 0x2EA4, 0x2EA5, 0x2EA6, 0x2EA7, 0x2EA8, 0x2EA9, 0x2EAA, 0x2EAB, 0x2EAC,
     0x2EAD, 0x2EAE, 0x2EAF, 0x2EB0, 0x2EB1, 0x2EB2, 0x2EB3, 0x2EB4, 0x2EB5, 0x2EB6, 0x2EB7, 0x2EB8, 0x2EB9, 0x2EBA,
     0x2EBB, 0x2EBC, 0x2EBD, 0x2EBE, 0x2EBF, 0x2EC0, 0x2EC1, 0x2EC2, 0x2EC3, 0x2EC4, 0x2EC5, 0x2EC6, 0x2EC7, 0x2EC8,
     0x2EC9, 0x2ECA, 0x2ECB, 0x2ECC, 0x2ECD, 0x2ECE, 0x2ECF, 0x2ED0, 0x2ED1, 0x2ED2, 0x2ED3, 0x2ED4, 0x2ED5, 0x2ED6,
     0x2ED7, 0x2ED8, 0x2ED9, 0x2EDA, 0x2EDB, 0x2EDC, 0x2EDD, 0x2EDE, 0x2EDF, 0x2EE0, 0x2EE1, 0x2EE2, 0x2EE3, 0x2EE4,
     0x2EE5, 0x2EE6, 0x2EE7, 0x2EE8, 0x2EE9, 0x2EEA, 0x2EEB, 0x2EEC, 0x2EED, 0x2EEE, 0x2EEF, 0x2EF0, 0x2EF1, 0x2EF2,
     0x2EF3, 0x3005, 0x3007, 0x303B, 0x3063, 0x3090, 0x3091, 0x309D, 0x309E, 0x30A0, 0x30A1, 0x30A2, 0x30A3, 0x30A4,
     0x30A5, 0x30A6, 0x30A7, 0x30A8, 0x30A9, 0x30AA, 0x30AB, 0x30AC, 0x30AD, 0x30AE, 0x30AF, 0x30B0, 0x30B1, 0x30B2,
     0x30B3, 0x30B4, 0x30B5, 0x30B6, 0x30B7, 0x30B8, 0x30B9, 0x30BA, 0x30BB, 0x30BC, 0x30BD, 0x30BE, 0x30BF, 0x30C0,
     0x30C1, 0x30C2, 0x30C3, 0x30C4, 0x30C5, 0x30C6, 0x30C7, 0x30C8, 0x30C9, 0x30CA, 0x30CB, 0x30CC, 0x30CD, 0x30CE,
     0x30CF, 0x30D0, 0x30D1, 0x30D2, 0x30D3, 0x30D4, 0x30D5, 0x30D6, 0x30D7, 0x30D8, 0x30D9, 0x30DA, 0x30DB, 0x30DC,
     0x30DD, 0x30DE, 0x30DF, 0x30E0, 0x30E1, 0x30E2, 0x30E3, 0x30E4, 0x30E5, 0x30E6, 0x30E7, 0x30E8, 0x30E9, 0x30EA,
     0x30EB, 0x30EC, 0x30ED, 0x30EE, 0x30EF, 0x30F0, 0x30F1, 0x30F2, 0x30F3, 0x30F4, 0x30F5, 0x30F6, 0x30FB, 0x30FD,
     0x30FE, 0x4E58, 0x4E82, 0x4E9E, 0x4F5B, 0x4F86, 0x5002, 0x5047, 0x50B3, 0x50DE, 0x50F9, 0x5109, 0x5152, 0x5167,
     0x5169, 0x518C, 0x5269, 0x528D, 0x5291, 0x52DE, 0x52F3, 0x52F5, 0x52F8, 0x5340, 0x5377, 0x537D, 0x53C3, 0x5433,
     0x543F, 0x55AE, 0x56B4, 0x56D1, 0x5708, 0x570B, 0x570D, 0x5713, 0x5716, 0x5718, 0x582F, 0x589E, 0x58AE, 0x58D3,
     0x58D8, 0x58DE, 0x58E4, 0x58EF, 0x58F9, 0x58FB, 0x58FD, 0x5967, 0x596C, 0x59EC, 0x5A1B, 0x5B43, 0x5B78, 0x5BE2,
     0x5BE6, 0x5BEB, 0x5BEC, 0x5BF6, 0x5C07, 0x5C08, 0x5C0D, 0x5C19, 0x5C46, 0x5C6C, 0x5CFD, 0x5DBD, 0x5DD6, 0x5DE2,
     0x5E36, 0x5EDA, 0x5EE2, 0x5EE3, 0x5EF3, 0x5F48, 0x5F4C, 0x5F91, 0x5F9E, 0x5FB5, 0x5FB7, 0x6046, 0x6085, 0x60E0,
     0x60E1, 0x60F1, 0x613C, 0x6158, 0x61C9, 0x61F7, 0x6200, 0x6230, 0x6232, 0x6236, 0x623E, 0x62C2, 0x62D4, 0x62DC,
     0x633E, 0x63D2, 0x63ED, 0x6416, 0x641C, 0x64C7, 0x64CA, 0x64D4, 0x64DA, 0x64E7, 0x64F4, 0x651D, 0x6536, 0x6548,
     0x654D, 0x654E, 0x6555, 0x6578, 0x65B7, 0x6649, 0x665A, 0x665D, 0x66C6, 0x66C9, 0x66FE, 0x6703, 0x689D, 0x68E7,
     0x69AE, 0x69C7, 0x6A02, 0x6A13, 0x6A1E, 0x6A23, 0x6A6B, 0x6AA2, 0x6AFB, 0x6B0A, 0x6B50, 0x6B61, 0x6B65, 0x6B72,
     0x6B77, 0x6B78, 0x6B98, 0x6BBC, 0x6BC6, 0x6BCF, 0x6C23, 0x6C92, 0x6D89, 0x6DDA, 0x6DE8, 0x6DFA, 0x6E34, 0x6EAA,
     0x6EAB, 0x6EEF, 0x6EFF, 0x6F5B, 0x6F81, 0x6FA4, 0x6FD5, 0x6FDF, 0x6FF1, 0x7027, 0x7028, 0x7063, 0x71C8, 0x71D2,
     0x71DF, 0x7210, 0x722D, 0x7232, 0x72A7, 0x72C0, 0x72F9, 0x7368, 0x7375, 0x7378, 0x737B, 0x7464, 0x74E3, 0x7501,
     0x7522, 0x756B, 0x7576, 0x758A, 0x7626, 0x7661, 0x767C, 0x76DC, 0x76E1, 0x771E, 0x784F, 0x788E, 0x7955, 0x797F,
     0x79AA, 0x79AE, 0x7A05, 0x7A31, 0x7A3B, 0x7A57, 0x7A69, 0x7A70, 0x7ACA, 0x7ADD, 0x7CB9, 0x7D55, 0x7D72, 0x7D93,
     0x7DA0, 0x7DD6, 0x7DE3, 0x7E23, 0x7E31, 0x7E3D, 0x7E69, 0x7E6A, 0x7E7C, 0x7E8C, 0x7E96, 0x7F3A, 0x7F50, 0x8070,
     0x8072, 0x807D, 0x8085, 0x812B, 0x8166, 0x81BD, 0x81DF, 0x81FA, 0x8207, 0x820A, 0x820D, 0x8216, 0x8277, 0x838A,
     0x8396, 0x842C, 0x85B0, 0x85CF, 0x85DD, 0x85E5, 0x8655, 0x865B, 0x865F, 0x87A2, 0x87F2, 0x8836, 0x883B, 0x885E,
     0x88DD, 0x8943, 0x89BA, 0x89BD, 0x89C0, 0x89F8, 0x8B20, 0x8B49, 0x8B6F, 0x8B7D, 0x8B80, 0x8B8A, 0x8B93, 0x8C50,
     0x8C6B, 0x8CB3, 0x8CE3, 0x8CF4, 0x8D0A, 0x8E10, 0x8F15, 0x8F49, 0x8FA8, 0x8FAD, 0x8FAF, 0x9059, 0x905E, 0x9072,
     0x908A, 0x90DE, 0x9115, 0x9189, 0x91AB, 0x91C0, 0x91CB, 0x92B3, 0x9304, 0x9322, 0x934A, 0x93AD, 0x9435, 0x9444,
     0x945B, 0x95DC, 0x9677, 0x96A8, 0x96AA, 0x96B1, 0x96B8, 0x96D9, 0x96DC, 0x9748, 0x9751, 0x975C, 0x984F, 0x986F,
     0x98EE, 0x9918, 0x9920, 0x9A37, 0x9A45, 0x9A57, 0x9A5B, 0x9AD3, 0x9AD4, 0x9AEE, 0x9B2A, 0x9DC4, 0x9E7D, 0x9EA5,
     0x9EB5, 0x9EC3, 0x9ED1, 0x9ED8, 0x9EDE, 0x9EE8, 0x9F4A, 0x9F4B, 0x9F52, 0x9F61, 0x9F8D, 0x9F9C, 0xD82C, 0xD85A,
     0xD87E, 0xDB40, 0xFE00, 0xFE01, 0xFE02, 0xFE03, 0xFE04, 0xFE05, 0xFE06, 0xFE07, 0xFE08, 0xFE09, 0xFE0A, 0xFE0B,
     0xFE0C, 0xFE0D, 0xFE0E, 0xFE0F}};

} // namespace maru::jp::tables
