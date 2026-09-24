#include "core/BuiltinPalettes.h"

#include "core/ColorSpace.h"

#include <QObject>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace kolorit::builtin {

namespace {

// Colors taken from the PNG palettes of the predecessor project, ImageRepainterForms (in their order)

/// Palette of the game Gartic Phone, 18 colors
constexpr unsigned kGarticPhone[] = {
    0x000000u, 0x666666u, 0x0050CDu, 0xFFFFFFu, 0xAAAAAAu, 0x26C9FFu, 0x017420u, 0x990000u,
    0x964112u, 0x11B03Cu, 0xFF0013u, 0xFF7829u, 0xB0701Cu, 0x99004Eu, 0xCB5A57u, 0xFFC126u,
    0xFF008Fu, 0xFEAFA8u,
};

/// MS Paint palette, 20 colors
constexpr unsigned kMsPaint[] = {
    0x000000u, 0x7F7F7Fu, 0x880015u, 0xED1C24u, 0xFF7F27u, 0xFFF200u, 0x22B14Cu, 0x00A2E8u,
    0x3F48CCu, 0xA349A4u, 0xFFFFFFu, 0xC3C3C3u, 0xB97A57u, 0xFFAEC9u, 0xFFC90Eu, 0xEFE4B0u,
    0xB5E61Du, 0x99D9EAu, 0x7092BEu, 0xC8BFE7u,
};

/// RAL Classic, 213 colors
constexpr unsigned kRalClassic[] = {
    0xCCC58Fu, 0xD1BC8Au, 0xD2B773u, 0xF7BA0Bu, 0xE2B007u, 0xC89F04u, 0xE1A100u, 0xE79C00u,
    0xAF8A54u, 0xD9C022u, 0xE9E5CEu, 0xDED09Fu, 0xEADEBDu, 0xEAF044u, 0xF4B752u, 0xF3E03Bu,
    0xA4957Du, 0x9A9464u, 0xEEC900u, 0xF0CA00u, 0xB89C50u, 0xF5FF00u, 0xA38C15u, 0xFFAB00u,
    0xDDB20Fu, 0xFAAB21u, 0xEDAB56u, 0xA29985u, 0x927549u, 0xEEA205u, 0xDD7907u, 0xBE4E24u,
    0xC63927u, 0xFA842Bu, 0xE75B12u, 0xFF2300u, 0xFFA421u, 0xF3752Cu, 0xE15501u, 0xD4652Fu,
    0xEC7C25u, 0xDB6A50u, 0x954527u, 0xAB2524u, 0xA02128u, 0xA1232Bu, 0x8D1D2Cu, 0x701F29u,
    0x5E2028u, 0x402225u, 0x703731u, 0x7E292Cu, 0xCB8D73u, 0x9C322Eu, 0xD47479u, 0xE1A6ADu,
    0xAC4034u, 0xD3545Fu, 0xD14152u, 0xC1121Cu, 0xD56D56u, 0xF70000u, 0xFF0000u, 0xB42041u,
    0xCB3334u, 0xAC323Bu, 0x711521u, 0xB24C43u, 0x8A5A83u, 0x933D50u, 0xD15B8Fu, 0x691639u,
    0x83639Du, 0x992572u, 0x4A203Bu, 0x904684u, 0xA38995u, 0xC63678u, 0x8773A1u, 0x6B6880u,
    0x384C70u, 0x1F4764u, 0x2B2C7Cu, 0x2A3756u, 0x1D1F2Au, 0x154889u, 0x41678Du, 0x313C48u,
    0x2E5978u, 0x13447Cu, 0x232C3Fu, 0x3481B8u, 0x232D53u, 0x6C7C98u, 0x2874B2u, 0x0E518Du,
    0x21888Fu, 0x1A5784u, 0x0B4151u, 0x07737Au, 0x2F2A5Au, 0x4D668Eu, 0x6A93B0u, 0x296478u,
    0x102C54u, 0x327662u, 0x28713Eu, 0x276235u, 0x4B573Eu, 0x0E4243u, 0x0F4336u, 0x40433Bu,
    0x283424u, 0x35382Eu, 0x26392Fu, 0x3E753Bu, 0x66825Bu, 0x31403Du, 0x797C5Au, 0x444337u,
    0x3D403Au, 0x026A52u, 0x468641u, 0x48A43Fu, 0xB7D9B1u, 0x354733u, 0x86A47Cu, 0x3E3C32u,
    0x008754u, 0x53753Cu, 0x005D52u, 0x81C0BBu, 0x2D5546u, 0x007243u, 0x0F8558u, 0x478A84u,
    0x7FB0B2u, 0x1B542Cu, 0x005D4Cu, 0x008F39u, 0x00BB2Eu, 0x7E8B92u, 0x8F999Fu, 0x817F68u,
    0x7A7B6Du, 0x9EA0A1u, 0x6B716Fu, 0x756F61u, 0x746643u, 0x5B6259u, 0x575D57u, 0x555D61u,
    0x596163u, 0x555548u, 0x51565Cu, 0x373F43u, 0x2E3234u, 0x4B4D46u, 0x818479u, 0x474A50u,
    0x374447u, 0x939388u, 0x5D6970u, 0xB9B9A8u, 0x818979u, 0x939176u, 0xCBD0CCu, 0x9A9697u,
    0x7C7F7Eu, 0xB4B8B0u, 0x6B695Fu, 0x9DA3A6u, 0x8F9695u, 0x4E5451u, 0xBDBDB2u, 0x91969Au,
    0x82898Eu, 0xCFD0CFu, 0x888175u, 0x887142u, 0x9C6B30u, 0x7B5141u, 0x80542Fu, 0x8F4E35u,
    0x6F4A2Fu, 0x6F4F28u, 0x5A3A29u, 0x673831u, 0x49392Du, 0x633A34u, 0x4C2F26u, 0x44322Du,
    0x3F3A3Au, 0x211F20u, 0xA65E2Fu, 0x79553Cu, 0x755C49u, 0x4E3B2Bu, 0x773C27u, 0xEFEBDCu,
    0xDDDED4u, 0xF4F8F4u, 0x2E3032u, 0x0A0A0Du, 0xA5A8A6u, 0x8F8F8Cu, 0xF7F9EFu, 0x292C2Fu,
    0xF7FBF5u, 0x2A2D2Fu, 0xCFD3CDu, 0x9C9C9Cu, 0x7E8182u,
};

/// Rainbow: a pastel sweep of hues, 360 colors
constexpr unsigned kRainbow[] = {
    0xD19494u, 0xD19594u, 0xD19694u, 0xD19794u, 0xD19894u, 0xD19994u, 0xD19A94u, 0xD19B94u,
    0xD19C94u, 0xD19D94u, 0xD19E94u, 0xD19F94u, 0xD1A094u, 0xD1A194u, 0xD1A294u, 0xD1A394u,
    0xD1A494u, 0xD1A594u, 0xD1A694u, 0xD1A794u, 0xD1A894u, 0xD1A994u, 0xD1AA94u, 0xD1AB94u,
    0xD1AC94u, 0xD1AD94u, 0xD1AE94u, 0xD1AF94u, 0xD1B094u, 0xD1B194u, 0xD1B294u, 0xD1B494u,
    0xD1B594u, 0xD1B694u, 0xD1B794u, 0xD1B894u, 0xD1B994u, 0xD1BA94u, 0xD1BB94u, 0xD1BC94u,
    0xD1BD94u, 0xD1BE94u, 0xD1BF94u, 0xD1C094u, 0xD1C194u, 0xD1C294u, 0xD1C394u, 0xD1C494u,
    0xD1C594u, 0xD1C694u, 0xD1C794u, 0xD1C894u, 0xD1C994u, 0xD1CA94u, 0xD1CB94u, 0xD1CC94u,
    0xD1CD94u, 0xD1CE94u, 0xD1CF94u, 0xD1D094u, 0xD1D194u, 0xD0D194u, 0xCFD194u, 0xCED194u,
    0xCDD194u, 0xCCD194u, 0xCBD194u, 0xCAD194u, 0xC9D194u, 0xC8D194u, 0xC7D194u, 0xC6D194u,
    0xC5D194u, 0xC4D194u, 0xC3D194u, 0xC2D194u, 0xC1D194u, 0xC0D194u, 0xBFD194u, 0xBED194u,
    0xBDD194u, 0xBCD194u, 0xBBD194u, 0xBAD194u, 0xB9D194u, 0xB8D194u, 0xB7D194u, 0xB6D194u,
    0xB5D194u, 0xB4D194u, 0xB2D194u, 0xB1D194u, 0xB0D194u, 0xAFD194u, 0xAED194u, 0xADD194u,
    0xACD194u, 0xABD194u, 0xAAD194u, 0xA9D194u, 0xA8D194u, 0xA7D194u, 0xA6D194u, 0xA5D194u,
    0xA4D194u, 0xA3D194u, 0xA2D194u, 0xA1D194u, 0xA0D194u, 0x9FD194u, 0x9ED194u, 0x9DD194u,
    0x9CD194u, 0x9BD194u, 0x9AD194u, 0x99D194u, 0x98D194u, 0x97D194u, 0x96D194u, 0x95D194u,
    0x94D194u, 0x94D195u, 0x94D196u, 0x94D197u, 0x94D198u, 0x94D199u, 0x94D19Au, 0x94D19Bu,
    0x94D19Cu, 0x94D19Du, 0x94D19Eu, 0x94D19Fu, 0x94D1A0u, 0x94D1A1u, 0x94D1A2u, 0x94D1A3u,
    0x94D1A4u, 0x94D1A5u, 0x94D1A6u, 0x94D1A7u, 0x94D1A8u, 0x94D1A9u, 0x94D1AAu, 0x94D1ABu,
    0x94D1ACu, 0x94D1ADu, 0x94D1AEu, 0x94D1AFu, 0x94D1B0u, 0x94D1B1u, 0x94D1B2u, 0x94D1B4u,
    0x94D1B5u, 0x94D1B6u, 0x94D1B7u, 0x94D1B8u, 0x94D1B9u, 0x94D1BAu, 0x94D1BBu, 0x94D1BCu,
    0x94D1BDu, 0x94D1BEu, 0x94D1BFu, 0x94D1C0u, 0x94D1C1u, 0x94D1C2u, 0x94D1C3u, 0x94D1C4u,
    0x94D1C5u, 0x94D1C6u, 0x94D1C7u, 0x94D1C8u, 0x94D1C9u, 0x94D1CAu, 0x94D1CBu, 0x94D1CCu,
    0x94D1CDu, 0x94D1CEu, 0x94D1CFu, 0x94D1D0u, 0x94D1D1u, 0x94D0D1u, 0x94CFD1u, 0x94CED1u,
    0x94CDD1u, 0x94CCD1u, 0x94CBD1u, 0x94CAD1u, 0x94C9D1u, 0x94C8D1u, 0x94C7D1u, 0x94C6D1u,
    0x94C5D1u, 0x94C4D1u, 0x94C3D1u, 0x94C2D1u, 0x94C1D1u, 0x94C0D1u, 0x94BFD1u, 0x94BED1u,
    0x94BDD1u, 0x94BCD1u, 0x94BBD1u, 0x94BAD1u, 0x94B9D1u, 0x94B8D1u, 0x94B7D1u, 0x94B6D1u,
    0x94B5D1u, 0x94B4D1u, 0x94B2D1u, 0x94B1D1u, 0x94B0D1u, 0x94AFD1u, 0x94AED1u, 0x94ADD1u,
    0x94ACD1u, 0x94ABD1u, 0x94AAD1u, 0x94A9D1u, 0x94A8D1u, 0x94A7D1u, 0x94A6D1u, 0x94A5D1u,
    0x94A4D1u, 0x94A3D1u, 0x94A2D1u, 0x94A1D1u, 0x94A0D1u, 0x949FD1u, 0x949ED1u, 0x949DD1u,
    0x949CD1u, 0x949BD1u, 0x949AD1u, 0x9499D1u, 0x9498D1u, 0x9497D1u, 0x9496D1u, 0x9495D1u,
    0x9494D1u, 0x9594D1u, 0x9694D1u, 0x9794D1u, 0x9894D1u, 0x9994D1u, 0x9A94D1u, 0x9B94D1u,
    0x9C94D1u, 0x9D94D1u, 0x9E94D1u, 0x9F94D1u, 0xA094D1u, 0xA194D1u, 0xA294D1u, 0xA394D1u,
    0xA494D1u, 0xA594D1u, 0xA694D1u, 0xA794D1u, 0xA894D1u, 0xA994D1u, 0xAA94D1u, 0xAB94D1u,
    0xAC94D1u, 0xAD94D1u, 0xAE94D1u, 0xAF94D1u, 0xB094D1u, 0xB194D1u, 0xB294D1u, 0xB494D1u,
    0xB594D1u, 0xB694D1u, 0xB794D1u, 0xB894D1u, 0xB994D1u, 0xBA94D1u, 0xBB94D1u, 0xBC94D1u,
    0xBD94D1u, 0xBE94D1u, 0xBF94D1u, 0xC094D1u, 0xC194D1u, 0xC294D1u, 0xC394D1u, 0xC494D1u,
    0xC594D1u, 0xC694D1u, 0xC794D1u, 0xC894D1u, 0xC994D1u, 0xCA94D1u, 0xCB94D1u, 0xCC94D1u,
    0xCD94D1u, 0xCE94D1u, 0xCF94D1u, 0xD094D1u, 0xD194D1u, 0xD194D0u, 0xD194CFu, 0xD194CEu,
    0xD194CDu, 0xD194CCu, 0xD194CBu, 0xD194CAu, 0xD194C9u, 0xD194C8u, 0xD194C7u, 0xD194C6u,
    0xD194C5u, 0xD194C4u, 0xD194C3u, 0xD194C2u, 0xD194C1u, 0xD194C0u, 0xD194BFu, 0xD194BEu,
    0xD194BDu, 0xD194BCu, 0xD194BBu, 0xD194BAu, 0xD194B9u, 0xD194B8u, 0xD194B7u, 0xD194B6u,
    0xD194B5u, 0xD194B4u, 0xD194B2u, 0xD194B1u, 0xD194B0u, 0xD194AFu, 0xD194AEu, 0xD194ADu,
    0xD194ACu, 0xD194ABu, 0xD194AAu, 0xD194A9u, 0xD194A8u, 0xD194A7u, 0xD194A6u, 0xD194A5u,
    0xD194A4u, 0xD194A3u, 0xD194A2u, 0xD194A1u, 0xD194A0u, 0xD1949Fu, 0xD1949Eu, 0xD1949Du,
    0xD1949Cu, 0xD1949Bu, 0xD1949Au, 0xD19499u, 0xD19498u, 0xD19497u, 0xD19496u, 0xD19495u,
};

// PPU 2C02 palette (NES), 64 entries; the repeated blacks are dropped when the palette is built
constexpr unsigned kNes[64] = {
    0x666666, 0x002A88, 0x1412A7, 0x3B00A4, 0x5C007E, 0x6E0040, 0x6C0600, 0x561D00,
    0x333500, 0x0B4800, 0x005200, 0x004F08, 0x00404D, 0x000000, 0x000000, 0x000000,
    0xADADAD, 0x155FD9, 0x4240FF, 0x7527FE, 0xA01ACC, 0xB71E7B, 0xB53120, 0x994E00,
    0x6B6D00, 0x388700, 0x0C9300, 0x008F32, 0x007C8D, 0x000000, 0x000000, 0x000000,
    0xFFFEFF, 0x64B0FF, 0x9290FF, 0xC676FF, 0xF36AFF, 0xFE6ECC, 0xFE8170, 0xEA9E22,
    0xBCBE00, 0x88D800, 0x5CE430, 0x45E082, 0x48CDDE, 0x4F4F4F, 0x000000, 0x000000,
    0xFFFEFF, 0xC0DFFF, 0xD3D2FF, 0xE8C8FF, 0xFBC2FF, 0xFEC4EA, 0xFECCC5, 0xF7D8A5,
    0xE4E594, 0xCFEF96, 0xBDF4AB, 0xB3F3CC, 0xB5EBF2, 0xB8B8B8, 0x000000, 0x000000,
};

/// ZX Spectrum: black, 7 normal, 7 bright (indices 8..14 are BRIGHT 1)
constexpr unsigned kZxSpectrum[15] = {
    0x000000, 0x0000D7, 0xD70000, 0xD700D7, 0x00D700, 0x00D7D7, 0xD7D700, 0xD7D7D7,
    0x0000FF, 0xFF0000, 0xFF00FF, 0x00FF00, 0x00FFFF, 0xFFFF00, 0xFFFFFF,
};

constexpr unsigned kGameBoy[4] = {0x0F380F, 0x306230, 0x8BAC0F, 0x9BBC0F};
constexpr unsigned kGameBoyPocket[4] = {0x000000, 0x555555, 0xAAAAAA, 0xFFFFFF};

// --- Interfaces and design systems ---
//
// Not meant to look like an old game but to fit a photo to a brand style or to the
// look of a website. The editor themes (Solarized, Nord, Gruvbox, Dracula, Catppuccin)
// are distributed under free licenses, and so are Material and Tailwind.

/// Solarized (Ethan Schoonover): eight background tones and eight accents
constexpr unsigned kSolarized[16] = {
    0x002B36, 0x073642, 0x586E75, 0x657B83, 0x839496, 0x93A1A1, 0xEEE8D5, 0xFDF6E3,
    0xB58900, 0xCB4B16, 0xDC322F, 0xD33682, 0x6C71C4, 0x268BD2, 0x2AA198, 0x859900,
};

/// Nord (Arctic Ice Studio): a cold northern range
constexpr unsigned kNord[16] = {
    0x2E3440, 0x3B4252, 0x434C5E, 0x4C566A, 0xD8DEE9, 0xE5E9F0, 0xECEFF4, 0x8FBCBB,
    0x88C0D0, 0x81A1C1, 0x5E81AC, 0xBF616A, 0xD08770, 0xEBCB8B, 0xA3BE8C, 0xB48EAD,
};

/// Gruvbox (Pavel Pertsev), dark variant: a warm muted range
constexpr unsigned kGruvbox[16] = {
    0x282828, 0xCC241D, 0x98971A, 0xD79921, 0x458588, 0xB16286, 0x689D6A, 0xA89984,
    0x928374, 0xFB4934, 0xB8BB26, 0xFABD2F, 0x83A598, 0xD3869B, 0x8EC07C, 0xEBDBB2,
};

/// Dracula (Zeno Rocha): dark background with bright pastel accents
constexpr unsigned kDracula[11] = {
    0x282A36, 0x44475A, 0x6272A4, 0xF8F8F2, 0x8BE9FD, 0x50FA7B,
    0xFFB86C, 0xFF79C6, 0xBD93F9, 0xFF5555, 0xF1FA8C,
};

/// Catppuccin Mocha: the full row from background to accents
constexpr unsigned kCatppuccin[26] = {
    0x11111B, 0x181825, 0x1E1E2E, 0x313244, 0x45475A, 0x585B70, 0x6C7086, 0x7F849C,
    0x9399B2, 0xA6ADC8, 0xBAC2DE, 0xCDD6F4, 0xF5E0DC, 0xF2CDCD, 0xF5C2E7, 0xCBA6F7,
    0xF38BA8, 0xEBA0AC, 0xFAB387, 0xF9E2AF, 0xA6E3A1, 0x94E2D5, 0x89DCEB, 0x74C7EC,
    0x89B4FA, 0xB4BEFE,
};

/// Material Design (Google): the primary 500 tones
constexpr unsigned kMaterial[19] = {
    0xF44336, 0xE91E63, 0x9C27B0, 0x673AB7, 0x3F51B5, 0x2196F3, 0x03A9F4, 0x00BCD4,
    0x009688, 0x4CAF50, 0x8BC34A, 0xCDDC39, 0xFFEB3B, 0xFFC107, 0xFF9800, 0xFF5722,
    0x795548, 0x9E9E9E, 0x607D8B,
};

/// Tailwind CSS: the row of 500 shades
constexpr unsigned kTailwind[22] = {
    0x64748B, 0x6B7280, 0x71717A, 0x737373, 0x78716C, 0xEF4444, 0xF97316, 0xF59E0B,
    0xEAB308, 0x84CC16, 0x22C55E, 0x10B981, 0x14B8A6, 0x06B6D4, 0x0EA5E9, 0x3B82F6,
    0x6366F1, 0x8B5CF6, 0xA855F7, 0xD946EF, 0xEC4899, 0xF43F5E,
};

// --- Pixel-art palettes ---
//
// Unlike hardware palettes, these were put together by artists so that the colors go
// well with each other and every hue has enough shadows and highlights. That is why on
// photos they give a noticeably more coherent result than palettes like CGA, whose
// colors were chosen by circuit design.
//
// These are sets their authors released for free use; the authors are named here and
// in the menu tooltips. Each palette has its own terms.

/// PICO-8 (Lexaloffle): the de facto standard of modern pixel art
constexpr unsigned kPico8[16] = {
    0x000000, 0x1D2B53, 0x7E2553, 0x008751, 0xAB5236, 0x5F574F, 0xC2C3C7, 0xFFF1E8,
    0xFF004D, 0xFFA300, 0xFFEC27, 0x00E436, 0x29ADFF, 0x83769C, 0xFF77A8, 0xFFCCAA,
};

/// DawnBringer 16: the classic set that started the "limited palette" genre
constexpr unsigned kDawnBringer16[16] = {
    0x140C1C, 0x442434, 0x30346D, 0x4E4A4E, 0x854C30, 0x346524, 0xD04648, 0x757161,
    0x597DCE, 0xD27D2C, 0x8595A1, 0x6DAA2C, 0xD2AA99, 0x6DC2CA, 0xDAD45E, 0xDEEED6,
};

/// DawnBringer 32: an extension of DB16, the most popular mid-sized set
constexpr unsigned kDawnBringer32[32] = {
    0x000000, 0x222034, 0x45283C, 0x663931, 0x8F563B, 0xDF7126, 0xD9A066, 0xEEC39A,
    0xFBF236, 0x99E550, 0x6ABE30, 0x37946E, 0x4B692F, 0x524B24, 0x323C39, 0x3F3F74,
    0x306082, 0x5B6EE1, 0x639BFF, 0x5FCDE4, 0xCBDBFC, 0xFFFFFF, 0x9BADB7, 0x847E87,
    0x696A6A, 0x595652, 0x76428A, 0xAC3232, 0xD95763, 0xD77BBA, 0x8F974A, 0x8A6F30,
};

/// Sweetie 16 (GrafxKid): a soft set with even tonal rows
constexpr unsigned kSweetie16[16] = {
    0x1A1C2C, 0x5D275D, 0xB13E53, 0xEF7D57, 0xFFCD75, 0xA7F070, 0x38B764, 0x257179,
    0x29366F, 0x3B5DC9, 0x41A6F6, 0x73EFF7, 0xF4F4F4, 0x94B0C2, 0x566C86, 0x333C57,
};

/// Endesga 32 (ENDESGA): a saturated set with a ready row of shadows for every hue
constexpr unsigned kEndesga32[32] = {
    0xBE4A2F, 0xD77643, 0xEAD4AA, 0xE4A672, 0xB86F50, 0x733E39, 0x3E2731, 0xA22633,
    0xE43B44, 0xF77622, 0xFEAE34, 0xFEE761, 0x63C74D, 0x3E8948, 0x265C42, 0x193C3E,
    0x124E89, 0x0099DB, 0x2CE8F5, 0xFFFFFF, 0xC0CBDC, 0x8B9BB4, 0x5A6988, 0x3A4466,
    0x262B44, 0x181425, 0xFF0044, 0x68386C, 0xB55088, 0xF6757A, 0xE8B796, 0xC28569,
};

// --- Retro platforms ---
//
// Values come from widely accepted reconstructions of the hardware palettes. For most
// machines there are no "one true" numbers: the color was produced in the analog video
// path, and emulators differ by a few levels.

/// Commodore 64, VIC-II: the Pepto reconstruction used by emulators
constexpr unsigned kCommodore64[16] = {
    0x000000, 0xFFFFFF, 0x880000, 0xAAFFEE, 0xCC44CC, 0x00CC55, 0x0000AA, 0xEEEE77,
    0xDD8855, 0x664400, 0xFF7777, 0x333333, 0x777777, 0xAAFF66, 0x0088FF, 0xBBBBBB,
};

/// IBM CGA, 16 colors (also the first 16 of EGA and VGA)
constexpr unsigned kCga16[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

/// CGA, mode 4: cyan / magenta / white, the most recognizable four colors of the era
constexpr unsigned kCgaCyan[4] = {0x000000, 0x55FFFF, 0xFF55FF, 0xFFFFFF};

/// CGA, mode 4: green / red / yellow
constexpr unsigned kCgaGreen[4] = {0x000000, 0x55FF55, 0xFF5555, 0xFFFF55};

/// MSX 1 / TMS9918: 15 visible colors (the transparent entry is dropped)
constexpr unsigned kMsx1[15] = {
    0x000000, 0x3EB849, 0x74D07D, 0x5955E0, 0x8076F1, 0xB95E51, 0x65DBEF, 0xDB6559,
    0xFF897D, 0xCCC35E, 0xDED087, 0x3AA241, 0xB766B5, 0xCCCCCC, 0xFFFFFF,
};

/// Apple Macintosh, the standard 16 colors of System 1–6
constexpr unsigned kMacSystem[16] = {
    0xFFFFFF, 0xFBF305, 0xFF6403, 0xDD0907, 0xF20884, 0x4700A5, 0x0000D3, 0x02ABEA,
    0x1FB714, 0x006412, 0x562C05, 0x90713A, 0xC0C0C0, 0x808080, 0x404040, 0x000000,
};

// --- Ramps: colors go strictly from shadows to highlights ---
//
// The gradient map and the duotone lay the palette out over luminance in stored order,
// so what matters here is not the set of colors but their sequence. Ordinary palettes
// do not work for this: as a gradient they zigzag.
//
// viridis, magma, inferno, plasma and cividis are the perceptually uniform colormaps of
// matplotlib, released by their authors into the public domain (CC0); turbo is Google's
// colormap (Apache 2.0). The key points of each map are listed: the gradient map
// interpolates between them itself, and for repainting they are a ready 10–12 color palette.

/// viridis, the main uniform map: dark violet → green → yellow
constexpr unsigned kViridis[] = {
    0x440154u, 0x482878u, 0x3E4A89u, 0x31688Eu, 0x26828Eu, 0x1F9E89u,
    0x35B779u, 0x6DCD59u, 0xB4DE2Cu, 0xDCE319u, 0xFDE725u,
};

/// magma: black → purple → light sand
constexpr unsigned kMagma[] = {
    0x000004u, 0x150E38u, 0x3B0F70u, 0x641A80u, 0x8C2981u, 0xB63679u,
    0xDE4968u, 0xF66E5Cu, 0xFE9F6Du, 0xFEC98Du, 0xFCFDBFu,
};

/// inferno: black → glowing orange → pale yellow
constexpr unsigned kInferno[] = {
    0x000004u, 0x160B39u, 0x420A68u, 0x6A176Eu, 0x932667u, 0xBC3754u,
    0xDD513Au, 0xF37819u, 0xFCA50Au, 0xF6D746u, 0xFCFFA4u,
};

/// plasma: blue → purple → yellow
constexpr unsigned kPlasma[] = {
    0x0D0887u, 0x41049Du, 0x6A00A8u, 0x8F0DA4u, 0xB12A90u, 0xCC4778u,
    0xE16462u, 0xF2844Bu, 0xFCA636u, 0xFCCE25u, 0xF0F921u,
};

/// cividis, designed for deuteranopia: blue → sand
constexpr unsigned kCividis[] = {
    0x00204Du, 0x00306Fu, 0x39486Bu, 0x575D6Du, 0x707173u,
    0x8A8678u, 0xA59C74u, 0xC3B369u, 0xE1CC55u, 0xFFEA46u,
};

/// turbo: a rainbow map without false edges, the replacement for the old jet
constexpr unsigned kTurbo[] = {
    0x30123Bu, 0x4454C4u, 0x4490FEu, 0x1FC8DEu, 0x29EFA2u, 0x7DFF56u,
    0xC1F334u, 0xF1CA3Au, 0xFE922Au, 0xEA4F0Du, 0xBE2102u, 0x7A0403u,
};

// Classic photographic tonings are ramps too, from shadow to highlight
constexpr unsigned kSepiaToning[] = {
    0x1F1206u, 0x4A2F14u, 0x8A5A2Bu, 0xC08D50u, 0xE3BE8Du, 0xF7ECD8u,
};
constexpr unsigned kCyanotype[] = {
    0x06121Fu, 0x0D2A47u, 0x1B4F7Au, 0x3E85AEu, 0x88BBD6u, 0xDCEEF7u,
};
constexpr unsigned kPlatinum[] = {
    0x141210u, 0x3A352Fu, 0x6B635Au, 0x9C948Au, 0xCCC6BCu, 0xF2EFE9u,
};
constexpr unsigned kSelenium[] = {
    0x130F16u, 0x33223Au, 0x5C3F5Eu, 0x8E6E85u, 0xC0A6B4u, 0xEDE2E6u,
};

template <size_t N>
Palette fromArray(const unsigned (&colors)[N])
{
    // Duplicates (such as the NES blacks) are dropped
    Palette palette;
    for (unsigned c : colors)
        palette.addUnique(QRgb(c | 0xFF000000u));
    return palette;
}

} // namespace

QString groupName(PaletteGroup group)
{
    switch (group) {
    case PaletteGroup::Ramp:     return QObject::tr("Ramps and gradients");
    case PaletteGroup::Retro:    return QObject::tr("Retro platforms");
    case PaletteGroup::PixelArt: return QObject::tr("Pixel art");
    case PaletteGroup::Ui:       return QObject::tr("UI themes");
    case PaletteGroup::Apps:     return QObject::tr("Apps and print");
    }
    return {};
}

QString groupHint(PaletteGroup group)
{
    switch (group) {
    case PaletteGroup::Ramp:
        return QObject::tr("Colors run from shadows to highlights: for gradient maps, duotones and heat maps");
    case PaletteGroup::Retro:
        return QObject::tr("Hardware palettes of old computers and consoles");
    case PaletteGroup::PixelArt:
        return QObject::tr("Palettes put together by artists for pixel art");
    case PaletteGroup::Ui:
        return QObject::tr("Color schemes of interfaces and design systems");
    case PaletteGroup::Apps:
        return QObject::tr("Palettes of applications, print and color matching");
    }
    return {};
}

QList<PaletteGroup> allGroups()
{
    return {PaletteGroup::Ramp, PaletteGroup::Retro, PaletteGroup::PixelArt, PaletteGroup::Ui,
            PaletteGroup::Apps};
}

QList<BuiltinPalette> palettes()
{
    return {
        // --- Ramps: the order of colors matters ---
        {QObject::tr("viridis"), fromArray(kViridis), PaletteGroup::Ramp},
        {QObject::tr("magma"), fromArray(kMagma), PaletteGroup::Ramp},
        {QObject::tr("inferno"), fromArray(kInferno), PaletteGroup::Ramp},
        {QObject::tr("plasma"), fromArray(kPlasma), PaletteGroup::Ramp},
        {QObject::tr("cividis (colorblind-friendly)"), fromArray(kCividis), PaletteGroup::Ramp},
        {QObject::tr("turbo (heat map)"), fromArray(kTurbo), PaletteGroup::Ramp},
        {QObject::tr("Sepia"), fromArray(kSepiaToning), PaletteGroup::Ramp},
        {QObject::tr("Cyanotype"), fromArray(kCyanotype), PaletteGroup::Ramp},
        {QObject::tr("Platinum print"), fromArray(kPlatinum), PaletteGroup::Ramp},
        {QObject::tr("Selenium toning"), fromArray(kSelenium), PaletteGroup::Ramp},

        // --- Retro platforms ---
        {QObject::tr("NES / Famicom"), nes(), PaletteGroup::Retro},
        {QObject::tr("ZX Spectrum"), zxSpectrum(), PaletteGroup::Retro},
        {QObject::tr("Game Boy"), gameBoy(), PaletteGroup::Retro},
        {QObject::tr("Game Boy Pocket"), gameBoyPocket(), PaletteGroup::Retro},
        {QObject::tr("Commodore 64"), fromArray(kCommodore64), PaletteGroup::Retro},
        {QObject::tr("IBM CGA"), fromArray(kCga16), PaletteGroup::Retro},
        {QObject::tr("CGA: cyan / magenta"), fromArray(kCgaCyan), PaletteGroup::Retro},
        {QObject::tr("CGA: green / red"), fromArray(kCgaGreen), PaletteGroup::Retro},
        // EGA and Amstrad CPC are even per-channel grids, so they are computed rather than stored
        {QObject::tr("IBM EGA"), levels(4), PaletteGroup::Retro},
        {QObject::tr("Amstrad CPC"), levels(3), PaletteGroup::Retro},
        {QObject::tr("MSX 1 / TMS9918"), fromArray(kMsx1), PaletteGroup::Retro},
        {QObject::tr("Macintosh System"), fromArray(kMacSystem), PaletteGroup::Retro},

        // --- Pixel art ---
        {QObject::tr("PICO-8"), fromArray(kPico8), PaletteGroup::PixelArt},
        {QObject::tr("DawnBringer 16"), fromArray(kDawnBringer16), PaletteGroup::PixelArt},
        {QObject::tr("DawnBringer 32"), fromArray(kDawnBringer32), PaletteGroup::PixelArt},
        {QObject::tr("Sweetie 16"), fromArray(kSweetie16), PaletteGroup::PixelArt},
        {QObject::tr("Endesga 32"), fromArray(kEndesga32), PaletteGroup::PixelArt},

        // --- UI themes ---
        {QObject::tr("Solarized"), fromArray(kSolarized), PaletteGroup::Ui},
        {QObject::tr("Nord"), fromArray(kNord), PaletteGroup::Ui},
        {QObject::tr("Gruvbox"), fromArray(kGruvbox), PaletteGroup::Ui},
        {QObject::tr("Dracula"), fromArray(kDracula), PaletteGroup::Ui},
        {QObject::tr("Catppuccin Mocha"), fromArray(kCatppuccin), PaletteGroup::Ui},
        {QObject::tr("Material Design"), fromArray(kMaterial), PaletteGroup::Ui},
        {QObject::tr("Tailwind CSS"), fromArray(kTailwind), PaletteGroup::Ui},

        // --- Apps and print ---
        {QObject::tr("Gartic Phone"), fromArray(kGarticPhone), PaletteGroup::Apps},
        {QObject::tr("MS Paint"), fromArray(kMsPaint), PaletteGroup::Apps},
        {QObject::tr("RAL Classic"), fromArray(kRalClassic), PaletteGroup::Apps},
        {QObject::tr("Web-safe colors"), webSafe(), PaletteGroup::Apps},
        {QObject::tr("Rainbow (360 hues)"), fromArray(kRainbow), PaletteGroup::Apps},
    };
}

QList<BuiltinPalette> palettes(PaletteGroup group)
{
    QList<BuiltinPalette> result;
    for (const BuiltinPalette& p : palettes())
        if (p.group == group)
            result.append(p);
    return result;
}

Palette byName(const QString& name)
{
    for (const BuiltinPalette& p : palettes())
        if (p.name == name)
            return p.palette;
    return {};
}

Palette grayscale(int count, bool perceptual)
{
    count = std::clamp(count, 2, 256);
    QVector<QRgb> list;
    list.reserve(count);
    for (int i = 0; i < count; ++i) {
        const double t = double(i) / (count - 1);
        if (perceptual) {
            // An even step in lightness: the steps are equal to the eye, not in bytes
            list.append(colorspace::labToRgb(float(100.0 * t), 0.f, 0.f));
        } else {
            const int v = int(std::lround(255.0 * t));
            list.append(qRgb(v, v, v));
        }
    }
    return Palette(list);
}

Palette tint(QRgb base, int count)
{
    count = std::clamp(count, 2, 256);

    float L = 0.f, a = 0.f, b = 0.f;
    colorspace::rgbToLab(qRed(base), qGreen(base), qBlue(base), L, a, b);
    const double hue = std::atan2(double(b), double(a));
    const double chroma = std::hypot(double(a), double(b));

    // Lightness runs from almost black to almost white and chroma falls off towards both
    // ends: extremely dark and extremely light colors simply cannot have any
    constexpr double kMinL = 4.0;
    constexpr double kMaxL = 97.0;

    QVector<QRgb> list;
    list.reserve(count);
    for (int i = 0; i < count; ++i) {
        const double t = double(i) / (count - 1);
        const double lightness = kMinL + (kMaxL - kMinL) * t;
        const double c = chroma * std::sin(std::numbers::pi * lightness / 100.0);
        list.append(colorspace::labToRgb(float(lightness), float(c * std::cos(hue)), float(c * std::sin(hue))));
    }
    return Palette(list);
}

namespace {

/// Linear interpolation of colors in CIE Lab: smoother transitions than in sRGB
QRgb mixLab(QRgb from, QRgb to, double t)
{
    float l1, a1, b1, l2, a2, b2;
    colorspace::rgbToLab(qRed(from), qGreen(from), qBlue(from), l1, a1, b1);
    colorspace::rgbToLab(qRed(to), qGreen(to), qBlue(to), l2, a2, b2);
    return colorspace::labToRgb(float(l1 + (l2 - l1) * t), float(a1 + (a2 - a1) * t),
                                float(b1 + (b2 - b1) * t));
}

} // namespace

Palette ramp(QRgb from, QRgb to, int count)
{
    count = std::clamp(count, 2, 256);
    QVector<QRgb> list;
    list.reserve(count);
    for (int i = 0; i < count; ++i)
        list.append(mixLab(from, to, double(i) / (count - 1)));
    return Palette(list);
}

Palette ramp(QRgb from, QRgb mid, QRgb to, int count)
{
    count = std::clamp(count, 3, 256);
    QVector<QRgb> list;
    list.reserve(count);
    for (int i = 0; i < count; ++i) {
        const double t = double(i) / (count - 1);
        list.append(t <= 0.5 ? mixLab(from, mid, t * 2.0) : mixLab(mid, to, (t - 0.5) * 2.0));
    }
    return Palette(list);
}

Palette levels(int levelsPerChannel)
{
    const int n = std::clamp(levelsPerChannel, 2, 16);
    QVector<QRgb> list;
    list.reserve(n * n * n);
    for (int r = 0; r < n; ++r)
        for (int g = 0; g < n; ++g)
            for (int b = 0; b < n; ++b)
                list.append(qRgb(int(std::lround(255.0 * r / (n - 1))), int(std::lround(255.0 * g / (n - 1))),
                                 int(std::lround(255.0 * b / (n - 1)))));
    return Palette(list);
}

Palette hueWheel(int count, double lightness, double chroma)
{
    count = std::clamp(count, 2, 256);
    const double L = std::clamp(lightness, 0.0, 100.0);
    const double C = std::clamp(chroma, 0.0, 128.0);

    QVector<QRgb> list;
    list.reserve(count);
    for (int i = 0; i < count; ++i) {
        const double h = 2.0 * std::numbers::pi * i / count;
        list.append(colorspace::labToRgb(float(L), float(C * std::cos(h)), float(C * std::sin(h))));
    }
    return Palette(list);
}

Palette gameBoy()
{
    return fromArray(kGameBoy);
}

Palette gameBoyPocket()
{
    return fromArray(kGameBoyPocket);
}

Palette nes()
{
    return fromArray(kNes);
}

Palette zxSpectrum()
{
    return fromArray(kZxSpectrum);
}

Palette webSafe()
{
    QVector<QRgb> list;
    list.reserve(216);
    for (int r = 0; r < 6; ++r)
        for (int g = 0; g < 6; ++g)
            for (int b = 0; b < 6; ++b)
                list.append(qRgb(r * 0x33, g * 0x33, b * 0x33));
    return Palette(list);
}

} // namespace kolorit::builtin
