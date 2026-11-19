#include "core/mappers/mapper_registry.hpp"

#include <bit>

#include "core/mappers/action52.hpp"
#include "core/mappers/axrom.hpp"
#include "core/mappers/bandai_fcg.hpp"
#include "core/mappers/bmc_t3h53.hpp"
#include "core/mappers/bxrom_wram.hpp"
#include "core/mappers/caltron_041.hpp"
#include "core/mappers/cartridge_story_274.hpp"
#include "core/mappers/city_fighter266.hpp"
#include "core/mappers/cnrom.hpp"
#include "core/mappers/cnrom185.hpp"
#include "core/mappers/color_dreams.hpp"
#include "core/mappers/datach.hpp"
#include "core/mappers/decathlon.hpp"
#include "core/mappers/discrete.hpp"
#include "core/mappers/fds_conversion108.hpp"
#include "core/mappers/ffe_super_magic_card.hpp"
#include "core/mappers/fk23c.hpp"
#include "core/mappers/flash_boards.hpp"
#include "core/mappers/fs304.hpp"
#include "core/mappers/g0151.hpp"
#include "core/mappers/golden_game.hpp"
#include "core/mappers/hp_multicart260.hpp"
#include "core/mappers/irem.hpp"
#include "core/mappers/irem_lrog017.hpp"
#include "core/mappers/jaleco_discrete.hpp"
#include "core/mappers/jaleco_jf13.hpp"
#include "core/mappers/jaleco_ss88006.hpp"
#include "core/mappers/jy_company.hpp"
#include "core/mappers/karaoke_studio_188.hpp"
#include "core/mappers/konami_vrc.hpp"
#include "core/mappers/mapper006.hpp"
#include "core/mappers/mapper012.hpp"
#include "core/mappers/mapper015.hpp"
#include "core/mappers/mapper027.hpp"
#include "core/mappers/mapper037.hpp"
#include "core/mappers/mapper042.hpp"
#include "core/mappers/mapper043.hpp"
#include "core/mappers/mapper044.hpp"
#include "core/mappers/mapper045.hpp"
#include "core/mappers/mapper047.hpp"
#include "core/mappers/mapper049.hpp"
#include "core/mappers/mapper052.hpp"
#include "core/mappers/mapper055.hpp"
#include "core/mappers/mapper056_ks202.hpp"
#include "core/mappers/mapper057.hpp"
#include "core/mappers/mapper083.hpp"
#include "core/mappers/mapper091.hpp"
#include "core/mappers/mapper103.hpp"
#include "core/mappers/mapper104_golden_five.hpp"
#include "core/mappers/mapper106.hpp"
#include "core/mappers/mapper112.hpp"
#include "core/mappers/mapper117.hpp"
#include "core/mappers/mapper120_lh15.hpp"
#include "core/mappers/mapper121.hpp"
#include "core/mappers/mapper122.hpp"
#include "core/mappers/mapper125_lh32.hpp"
#include "core/mappers/mapper126.hpp"
#include "core/mappers/mapper128.hpp"
#include "core/mappers/mapper142_ks202.hpp"
#include "core/mappers/mapper144.hpp"
#include "core/mappers/mapper156_daou.hpp"
#include "core/mappers/mapper162_fs304.hpp"
#include "core/mappers/mapper165.hpp"
#include "core/mappers/mapper166.hpp"
#include "core/mappers/mapper174.hpp"
#include "core/mappers/mapper177.hpp"
#include "core/mappers/mapper183.hpp"
#include "core/mappers/mapper187.hpp"
#include "core/mappers/mapper190.hpp"
#include "core/mappers/mapper191.hpp"
#include "core/mappers/mapper222_ctc31.hpp"
#include "core/mappers/mapper224.hpp"
#include "core/mappers/mapper257.hpp"
#include "core/mappers/mapper267.hpp"
#include "core/mappers/mapper268_coolboy.hpp"
#include "core/mappers/mapper269.hpp"
#include "core/mappers/mapper270.hpp"
#include "core/mappers/mapper271.hpp"
#include "core/mappers/mapper272.hpp"
#include "core/mappers/mapper273.hpp"
#include "core/mappers/mapper283.hpp"
#include "core/mappers/mapper284_drip.hpp"
#include "core/mappers/mapper285.hpp"
#include "core/mappers/mapper286_benshieng.hpp"
#include "core/mappers/mapper287.hpp"
#include "core/mappers/mapper288.hpp"
#include "core/mappers/mapper289.hpp"
#include "core/mappers/mapper290.hpp"
#include "core/mappers/mapper291.hpp"
#include "core/mappers/mapper292.hpp"
#include "core/mappers/mapper293.hpp"
#include "core/mappers/mapper294.hpp"
#include "core/mappers/mapper297.hpp"
#include "core/mappers/mapper299.hpp"
#include "core/mappers/mapper300.hpp"
#include "core/mappers/mapper301.hpp"
#include "core/mappers/mapper302_ks7057.hpp"
#include "core/mappers/mapper304.hpp"
#include "core/mappers/mapper305.hpp"
#include "core/mappers/mapper306.hpp"
#include "core/mappers/mapper307.hpp"
#include "core/mappers/mapper308.hpp"
#include "core/mappers/mapper309.hpp"
#include "core/mappers/mapper310.hpp"
#include "core/mappers/mapper312.hpp"
#include "core/mappers/mapper313.hpp"
#include "core/mappers/mapper314.hpp"
#include "core/mappers/mapper315.hpp"
#include "core/mappers/mapper319.hpp"
#include "core/mappers/mapper320.hpp"
#include "core/mappers/mapper321.hpp"
#include "core/mappers/mapper322.hpp"
#include "core/mappers/mapper323.hpp"
#include "core/mappers/mapper324.hpp"
#include "core/mappers/mapper325.hpp"
#include "core/mappers/mapper326.hpp"
#include "core/mappers/mapper327.hpp"
#include "core/mappers/mapper328.hpp"
#include "core/mappers/mapper330.hpp"
#include "core/mappers/mapper331.hpp"
#include "core/mappers/mapper332.hpp"
#include "core/mappers/mapper333.hpp"
#include "core/mappers/mapper334.hpp"
#include "core/mappers/mapper335.hpp"
#include "core/mappers/mapper336.hpp"
#include "core/mappers/mapper337.hpp"
#include "core/mappers/mapper338.hpp"
#include "core/mappers/mapper339.hpp"
#include "core/mappers/mapper340.hpp"
#include "core/mappers/mapper341.hpp"
#include "core/mappers/mapper342.hpp"
#include "core/mappers/mapper344.hpp"
#include "core/mappers/mapper345.hpp"
#include "core/mappers/mapper346.hpp"
#include "core/mappers/mapper347.hpp"
#include "core/mappers/mapper348.hpp"
#include "core/mappers/mapper349.hpp"
#include "core/mappers/mapper350.hpp"
#include "core/mappers/mapper351.hpp"
#include "core/mappers/mapper353.hpp"
#include "core/mappers/mapper354.hpp"
#include "core/mappers/mapper355.hpp"
#include "core/mappers/mapper356.hpp"
#include "core/mappers/mapper357.hpp"
#include "core/mappers/mapper359.hpp"
#include "core/mappers/mapper360.hpp"
#include "core/mappers/mapper361.hpp"
#include "core/mappers/mapper362.hpp"
#include "core/mappers/mapper364.hpp"
#include "core/mappers/mapper365.hpp"
#include "core/mappers/mapper366.hpp"
#include "core/mappers/mapper367.hpp"
#include "core/mappers/mapper368.hpp"
#include "core/mappers/mapper369.hpp"
#include "core/mappers/mapper370.hpp"
#include "core/mappers/mapper371.hpp"
#include "core/mappers/mapper372.hpp"
#include "core/mappers/mapper373.hpp"
#include "core/mappers/mapper374.hpp"
#include "core/mappers/mapper375.hpp"
#include "core/mappers/mapper376.hpp"
#include "core/mappers/mapper377.hpp"
#include "core/mappers/mapper378.hpp"
#include "core/mappers/mapper379.hpp"
#include "core/mappers/mapper380.hpp"
#include "core/mappers/mapper381.hpp"
#include "core/mappers/mapper382.hpp"
#include "core/mappers/mapper383.hpp"
#include "core/mappers/mapper384.hpp"
#include "core/mappers/mapper385.hpp"
#include "core/mappers/mapper389.hpp"
#include "core/mappers/mapper391.hpp"
#include "core/mappers/mapper392.hpp"
#include "core/mappers/mapper393.hpp"
#include "core/mappers/mapper394.hpp"
#include "core/mappers/mapper395.hpp"
#include "core/mappers/mapper396.hpp"
#include "core/mappers/mapper398.hpp"
#include "core/mappers/mapper399.hpp"
#include "core/mappers/mapper400.hpp"
#include "core/mappers/mapper401.hpp"
#include "core/mappers/mapper402.hpp"
#include "core/mappers/mapper403.hpp"
#include "core/mappers/mapper404.hpp"
#include "core/mappers/mapper405.hpp"
#include "core/mappers/mapper406.hpp"
#include "core/mappers/mapper407.hpp"
#include "core/mappers/mapper408.hpp"
#include "core/mappers/mapper409.hpp"
#include "core/mappers/mapper410.hpp"
#include "core/mappers/mapper411.hpp"
#include "core/mappers/mapper412.hpp"
#include "core/mappers/mapper414.hpp"
#include "core/mappers/mapper415.hpp"
#include "core/mappers/mapper416.hpp"
#include "core/mappers/mapper417.hpp"
#include "core/mappers/mapper419.hpp"
#include "core/mappers/mapper422.hpp"
#include "core/mappers/mapper423.hpp"
#include "core/mappers/mapper424.hpp"
#include "core/mappers/mapper425.hpp"
#include "core/mappers/mapper426.hpp"
#include "core/mappers/mapper427.hpp"
#include "core/mappers/maxi15.hpp"
#include "core/mappers/mmc1.hpp"
#include "core/mappers/mmc1a.hpp"
#include "core/mappers/mmc2.hpp"
#include "core/mappers/mmc3.hpp"
#include "core/mappers/mmc4.hpp"
#include "core/mappers/mmc5.hpp"
#include "core/mappers/multicart060.hpp"
#include "core/mappers/multicart063.hpp"
#include "core/mappers/multicart227.hpp"
#include "core/mappers/multicart230.hpp"
#include "core/mappers/multicart233.hpp"
#include "core/mappers/multicart242.hpp"
#include "core/mappers/n32_mapper050.hpp"
#include "core/mappers/namco108.hpp"
#include "core/mappers/namco163.hpp"
#include "core/mappers/namco175340.hpp"
#include "core/mappers/nanjing_fc001.hpp"
#include "core/mappers/nes_event.hpp"
#include "core/mappers/nrom.hpp"
#include "core/mappers/ntdec2746.hpp"
#include "core/mappers/ntdec_2722.hpp"
#include "core/mappers/ntdec_n625092.hpp"
#include "core/mappers/oeka_kids.hpp"
#include "core/mappers/racermate168.hpp"
#include "core/mappers/rambo1.hpp"
#include "core/mappers/realtec_multicart.hpp"
#include "core/mappers/ret_cufrom.hpp"
#include "core/mappers/sachen.hpp"
#include "core/mappers/single_chip.hpp"
#include "core/mappers/sl1632.hpp"
#include "core/mappers/somari_p_116.hpp"
#include "core/mappers/sunsoft34.hpp"
#include "core/mappers/sunsoft5b.hpp"
#include "core/mappers/super_game_mega_type_iii.hpp"
#include "core/mappers/t4a54a.hpp"
#include "core/mappers/taito_tc.hpp"
#include "core/mappers/taito_x1005.hpp"
#include "core/mappers/taito_x1017.hpp"
#include "core/mappers/teletubbies.hpp"
#include "core/mappers/txc.hpp"
#include "core/mappers/txc189.hpp"
#include "core/mappers/unl_tf1201.hpp"
#include "core/mappers/uxrom.hpp"
#include "core/mappers/vrc6.hpp"
#include "core/mappers/vrc7.hpp"
#include "core/mappers/vs_system.hpp"
#include "core/mappers/vt3x.hpp"

namespace mapperbus::core {
namespace {

[[nodiscard]] Result<void> validate_geometry(const INesHeader& header,
                                             std::size_t prg_size,
                                             std::size_t chr_size) {
    if (header.mapper_number == 31 && prg_size < 4096) {
        return std::unexpected("Mapper 31 requires at least a 4 KiB PRG ROM");
    }
    if (header.mapper_number == 6U || header.mapper_number == 8U)
        return FfeMagicCard::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 27U)
        return Mapper027::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 42U)
        return Mapper042::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 43U)
        return Mapper043::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 44U)
        return Mapper044::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 45U)
        return Mapper045::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 47U)
        return Mmc3Multicart47::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 50U)
        return N32Mapper050::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 52U)
        return Realtec8213::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 55U)
        return Mario1Malee2::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 59U)
        return BmcT3h53::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 60U)
        return Multicart060::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 63U)
        return Multicart063::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 83U || header.mapper_number == 264U)
        return ConyYoko::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 96U)
        return OekaKids::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 105U)
        return NesEvent::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 108U)
        return FdsConversion108::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 112U)
        return Mapper112::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 120U)
        return Lh15::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 122U)
        return Jy043::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 134U)
        return T4a54a::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 142U)
        return KaiserKs202::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 164U)
        return Fs304::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 166U || header.mapper_number == 167U)
        return Mapper166::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 174U)
        return Ntdec174::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 183U)
        return Mapper183::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 189U)
        return Txc189::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 190U)
        return Mapper190::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 191U)
        return Mapper191::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 261U)
        return Ntdec2746::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 274U)
        return CartridgeStory274::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 283U)
        return BlockFamily283::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 12U)
        return Mapper012::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 14U)
        return Sl1632::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 15U)
        return Mapper015::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 17U)
        return FfeSuperMagicCard::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 29U)
        return RetCufrom::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 37U)
        return Mmc3Multicart37::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 40U)
        return Ntdec2722::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 41U)
        return Caltron041::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 49U)
        return Mapper049::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 56U)
        return Mapper056Ks202::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 57U)
        return Mapper057::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 86U)
        return JalecoJf13::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 91U)
        return Mapper091::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 103U)
        return Mapper103::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 104U)
        return Mapper104GoldenFive::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 106U)
        return Mapper106::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 116U)
        return SomariP116::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 117U)
        return Mapper117::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 121U)
        return Mapper121::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 124U)
        return SuperGameMegaTypeIII::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 125U)
        return Lh32::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 126U)
        return Tec9719::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 128U || header.mapper_number == 265U)
        return Mapper128::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 155U)
        return Mmc1a::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 144U)
        return Mapper144::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 156U)
        return DaouMapper156::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 162U)
        return Mapper162Fs304::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 163U)
        return NanjingFc001::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 165U)
        return Mapper165::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 168U)
        return Racermate168::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 176U)
        return Fk23c::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 177U)
        return Henggedianzi177::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 185U)
        return Cnrom185::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 187U)
        return Mapper187::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 188U)
        return KaraokeStudio188::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 224U)
        return Kt008::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 257U)
        return Mapper257::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 260U)
        return HpMulticart260::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 266U)
        return CityFighter266::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 267U)
        return Mapper267::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 268U)
        return Coolboy::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 269U)
        return Mapper269::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 271U)
        return Bmc22026::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 272U)
        return Mapper272::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 273U)
        return Mapper273::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 99U)
        return VsSystem::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 222U)
        return Ctc31::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 270U)
        return Mapper270::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 284U)
        return Drip::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 285U)
        return Mapper285::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 286U)
        return BenshiengBs5::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 287U)
        return Mapper287::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 288U)
        return Mapper288::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 289U)
        return Mapper289::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 290U)
        return Mapper290::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 291U)
        return Mapper291::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 292U)
        return Mapper292::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 293U)
        return Mapper293::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 294U)
        return Mapper294::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 296U)
        return Vt3x::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 297U)
        return Mapper297::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 298U)
        return UnlTf1201::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 299U)
        return Mapper299::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 300U)
        return Mapper300::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 301U)
        return Mapper301::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 302U)
        return KaiserKs7057::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 304U)
        return Mapper304::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 305U)
        return Mapper305::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 306U)
        return Mapper306::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 307U)
        return Mapper307::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 308U)
        return Mapper308::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 309U)
        return Mapper309::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 310U)
        return Mapper310::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 312U)
        return Mapper312::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 313U)
        return Mapper313::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 314U)
        return Mapper314::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 315U)
        return Mapper315::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 319U)
        return Mapper319::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 320U)
        return Mapper320::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 321U)
        return Mapper321::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 322U)
        return Mapper322::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 323U)
        return FaridSlrom323::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 324U)
        return Mapper324::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 325U)
        return Mapper325::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 326U)
        return Mapper326::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 327U)
        return Mapper327::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 328U)
        return Mapper328::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 330U)
        return Mapper330::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 331U)
        return Mapper331::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 332U)
        return Mapper332::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 333U)
        return Mapper333::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 334U)
        return Mapper334::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 335U)
        return Mapper335::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 336U)
        return Mapper336::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 337U)
        return Mapper337::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 338U)
        return Mapper338::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 339U)
        return Mapper339::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 340U)
        return Mapper340::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 341U)
        return Mapper341::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 342U)
        return Mapper342::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 344U)
        return Mapper344::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 345U)
        return Mapper345::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 346U)
        return Mapper346::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 347U)
        return Mapper347::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 348U)
        return Mapper348::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 349U)
        return Mapper349::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 350U)
        return Mapper350::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 351U)
        return Mapper351::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 353U)
        return Mapper353::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 354U)
        return Mapper354::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 355U)
        return Mapper355::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 356U)
        return Mapper356::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 357U)
        return Mapper357::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 359U)
        return Mapper359::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 360U)
        return Mapper360::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 361U)
        return Mapper361::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 362U)
        return Mapper362::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 364U)
        return Mapper364::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 365U)
        return Mapper365::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 366U)
        return Mapper366::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 367U)
        return Mapper367::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 368U)
        return Mapper368::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 369U)
        return Mapper369::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 370U)
        return Mapper370::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 371U)
        return Mapper371::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 372U)
        return Mapper372::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 373U)
        return Mapper373::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 374U)
        return Mapper374::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 375U)
        return Mapper375::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 376U)
        return Mapper376::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 377U)
        return Mapper377::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 378U)
        return Mapper378::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 379U)
        return Mapper379::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 380U)
        return Mapper380::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 381U)
        return Mapper381::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 382U)
        return Mapper382::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 383U)
        return Mapper383::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 384U)
        return Mapper384::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 385U)
        return Mapper385::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 389U)
        return Mapper389::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 391U)
        return Mapper391::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 392U)
        return Mapper392::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 393U)
        return Mapper393::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 394U)
        return Mapper394::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 395U)
        return Mapper395::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 396U)
        return Mapper396::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 398U)
        return Mapper398::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 399U)
        return Mapper399::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 400U)
        return Mapper400::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 401U)
        return Mapper401::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 402U)
        return Mapper402::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 403U)
        return Mapper403::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 404U)
        return Mapper404::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 405U)
        return Mapper405::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 406U)
        return Mapper406::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 407U)
        return Mapper407::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 408U)
        return Mapper408::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 409U)
        return Mapper409::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 410U)
        return Mapper410::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 411U)
        return Mapper411::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 412U)
        return Mapper412::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 414U)
        return Mapper414::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 415U)
        return Mapper415::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 416U)
        return Mapper416::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 417U)
        return Mapper417::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 419U)
        return Mapper419::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 422U)
        return Mapper422::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 423U)
        return Mapper423::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 424U)
        return Mapper424::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 425U)
        return Mapper425::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 426U)
        return Mapper426::validate_geometry(header, prg_size, chr_size, Mapper426::kSerialRomSize);
    if (header.mapper_number == 427U)
        return Mapper427::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 30U || header.mapper_number == 111U)
        return FlashBoards::validate_geometry(header, prg_size, chr_size);
    if (JyCompany::supports_mapper(header.mapper_number))
        return JyCompany::validate_geometry(header, prg_size, chr_size);
    if (Sachen::supports_mapper(header.mapper_number))
        return Sachen::validate_geometry(header, prg_size, chr_size);
    if (Txc::supports_mapper(header.mapper_number))
        return Txc::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 221U)
        return NtdecN625092::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 227U)
        return Multicart227::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 228U)
        return Action52::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 230U)
        return Multicart230::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 233U)
        return Multicart233::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 234U)
        return Maxi15::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 237U)
        return Teletubbies::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 236U)
        return RealtecMulticart::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 235U)
        return GoldenGame::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 254U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 254 supports submapper 0");
        if (prg_size != 131072U || chr_size != 131072U)
            return std::unexpected("Mapper 254 requires 128 KiB PRG and 128 KiB CHR ROM");
        if (header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            (header.is_nes2 && (header.prg_ram_size != (header.has_battery ? 0U : 8192U) ||
                                header.prg_nvram_size != (header.has_battery ? 8192U : 0U))))
            return std::unexpected("Mapper 254 requires 8 KiB WRAM and no CHR RAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 254 supports H/V startup mirroring");
        return {};
    }
    if (header.mapper_number == 253U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 253 supports submapper 0");
        if (prg_size < 32768U || prg_size > 262144U || !std::has_single_bit(prg_size) ||
            chr_size < 8192U || chr_size > 524288U || !std::has_single_bit(chr_size))
            return std::unexpected(
                "Mapper 253 requires power-of-two 32-256 KiB PRG and 8-512 KiB CHR ROM");
        if (header.chr_nvram_size != 0U ||
            (header.is_nes2 && (header.chr_ram_size != 2048U ||
                                header.prg_ram_size != (header.has_battery ? 0U : 8192U) ||
                                header.prg_nvram_size != (header.has_battery ? 8192U : 0U))))
            return std::unexpected("Mapper 253 requires 8 KiB WRAM and 2 KiB volatile CHR RAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 253 supports H/V startup mirroring");
        return {};
    }
    if (header.mapper_number == 252U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 252 supports submapper 0");
        if (prg_size != 262144U || chr_size != 131072U)
            return std::unexpected("Mapper 252 requires 256 KiB PRG and 128 KiB CHR ROM");
        if (header.chr_nvram_size != 0U ||
            (header.is_nes2 && (header.chr_ram_size != 2048U ||
                                header.prg_ram_size != (header.has_battery ? 0U : 8192U) ||
                                header.prg_nvram_size != (header.has_battery ? 8192U : 0U))))
            return std::unexpected("Mapper 252 requires 8 KiB WRAM and 2 KiB volatile CHR RAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 252 supports H/V startup mirroring");
        return {};
    }
    if (header.mapper_number == 246U)
        return G0151::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 245U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 245 supports submapper 0");
        if (prg_size < 32768U || prg_size > 1048576U || !std::has_single_bit(prg_size) ||
            chr_size != 0U)
            return std::unexpected(
                "Mapper 245 requires power-of-two 32 KiB-1 MiB PRG and no CHR ROM");
        if (!header.has_battery ||
            (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 8192U ||
                                header.chr_ram_size != 8192U || header.chr_nvram_size != 0U)))
            return std::unexpected(
                "Mapper 245 requires 8 KiB battery WRAM and 8 KiB volatile CHR RAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 245 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 244U)
        return Decathlon::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 242U)
        return Multicart242::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 241U)
        return BxromWram::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 238U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 238 supports submapper 0");
        if (prg_size != 0x20000U || chr_size != 0x40000U)
            return std::unexpected("Mapper 238 supports 128 KiB PRG and 256 KiB CHR ROM");
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
            return std::unexpected("Mapper 238 supports ROM-only board memory");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 238 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 219U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 219 supports submapper 0");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x80000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-219 ROM geometry");
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
            return std::unexpected("Mapper 219 supports ROM-only board memory");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 219 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 218U)
        return SingleChip::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 215U) {
        if (header.is_nes2 && header.submapper > 1U)
            return std::unexpected("Mapper 215 supports submappers 0 and 1");
        const auto max_rom = header.is_nes2 && header.submapper == 1U ? 0x200000U : 0x100000U;
        if (prg_size < 0x8000U || prg_size > max_rom || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > max_rom || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-215 ROM geometry");
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
            return std::unexpected("Mapper 215 supports ROM-only board memory");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 215 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 208U) {
        if (header.is_nes2 && header.submapper > 1U)
            return std::unexpected("Mapper 208 supports submappers 0 and 1");
        const auto max_prg = header.is_nes2 && header.submapper == 1U ? 0x200000U : 0x20000U;
        if (prg_size < 0x8000U || prg_size > max_prg || prg_size % 0x8000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-208 ROM geometry");
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
            return std::unexpected("Mapper 208 supports ROM-only board memory");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 208 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 205U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 205 supports submapper 0");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x80000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-205 ROM geometry");
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
            return std::unexpected("Mapper 205 supports ROM-only board memory");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 205 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 202U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 202 supports submapper 0");
        if (prg_size < 0x4000U || prg_size > 0x20000U || prg_size % 0x4000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x10000U || chr_size % 0x2000U != 0U)
            return std::unexpected("Unsupported mapper-202 ROM geometry");
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
            return std::unexpected("Mapper 202 supports ROM-only board memory");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 202 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 199U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 199 supports submapper 0");
        if (prg_size < 0x8000U || prg_size > 0x100000U || prg_size % 0x2000U != 0U ||
            chr_size != 0U)
            return std::unexpected("Unsupported mapper-199 ROM geometry");
        if (!header.has_battery || header.chr_nvram_size != 0U ||
            (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0x8000U ||
                                header.chr_ram_size != 0x2000U)))
            return std::unexpected(
                "Mapper 199 requires a 32 KiB PRG NVRAM chip and 8 KiB volatile CHR RAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 199 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 198U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 198 supports submapper 0");
        if (prg_size != 0xA0000U || chr_size != 0U)
            return std::unexpected(
                "Mapper 198 requires 640 KiB PRG ROM and CHR RAM without CHR ROM");
        if (!header.has_battery || header.chr_nvram_size != 0U ||
            (header.is_nes2 &&
             (header.prg_ram_size != 0x1000U || header.prg_nvram_size != 0x2000U ||
              header.chr_ram_size != 0x2000U)))
            return std::unexpected(
                "Mapper 198 requires 4 KiB volatile CPU RAM, 8 KiB PRG NVRAM and 8 KiB CHR RAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 198 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 197U) {
        if (header.is_nes2 && header.submapper > 3U)
            return std::unexpected("Mapper 197 supports submappers 0 through 3");
        const bool prg_mux = header.is_nes2 && header.submapper == 3U;
        if (prg_size < 0x8000U || prg_size > (prg_mux ? 0x40000U : 0x80000U) ||
            prg_size % 0x2000U != 0U || chr_size < 0x2000U || chr_size > 0x80000U ||
            chr_size % 0x800U != 0U)
            return std::unexpected("Unsupported mapper-197 ROM geometry");
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
            return std::unexpected("Mapper 197 supports ROM-only board memory");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 197 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 196U) {
        if (header.is_nes2 && header.submapper > 3U)
            return std::unexpected("Mapper 196 supports submappers 0 through 3");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-196 ROM geometry");
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
            return std::unexpected("Mapper 196 supports ROM-only board memory");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 196 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 195U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 195 supports submapper 0");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-195 ROM geometry");
        if (header.chr_nvram_size != 0U || (header.is_nes2 && header.chr_ram_size != 0x2000U))
            return std::unexpected("Mapper 195 requires 8 KiB of addressable volatile CHR RAM");
        if (header.is_nes2) {
            if ((header.prg_ram_size != 0U && header.prg_ram_size != 0x1000U &&
                 header.prg_ram_size != 0x2000U) ||
                (header.prg_nvram_size != 0U && header.prg_nvram_size != 0x2000U) ||
                (header.prg_ram_size == 0x2000U && header.prg_nvram_size != 0U))
                return std::unexpected(
                    "Mapper 195 supports optional 4 KiB auxiliary RAM and one 8 KiB WRAM chip");
            if (header.has_battery != (header.prg_nvram_size != 0U))
                return std::unexpected("Mapper 195 battery flag and NVRAM declaration disagree");
        }
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 195 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 74U || header.mapper_number == 192U ||
        header.mapper_number == 194U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mixed-CHR MMC3 boards support submapper 0");
        const bool ram_only = header.mapper_number == 194U && chr_size == 0U;
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            (!ram_only && (chr_size < 0x2000U || chr_size > 0x40000U || chr_size % 0x400U != 0U)))
            return std::unexpected("Unsupported mixed-CHR MMC3 ROM geometry");
        if (header.chr_nvram_size != 0U ||
            (header.is_nes2 && header.chr_ram_size != 0x800U && header.chr_ram_size != 0x1000U))
            return std::unexpected("Mixed-CHR MMC3 boards require 2 or 4 KiB of volatile CHR RAM");
        if (header.is_nes2) {
            if ((header.prg_ram_size != 0U && header.prg_ram_size != 0x2000U) ||
                (header.prg_nvram_size != 0U && header.prg_nvram_size != 0x2000U) ||
                (header.prg_ram_size != 0U && header.prg_nvram_size != 0U))
                return std::unexpected(
                    "Mixed-CHR MMC3 boards support one optional 8 KiB WRAM chip");
            if (header.has_battery != (header.prg_nvram_size != 0U))
                return std::unexpected(
                    "Mixed-CHR MMC3 battery flag and NVRAM declaration disagree");
        }
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mixed-CHR MMC3 boards support ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 249U ||
        (header.mapper_number == 4U && header.is_nes2 && header.submapper == 5U)) {
        if (header.mapper_number == 249U && header.is_nes2 && header.submapper != 0U)
            return std::unexpected(
                "Mapper 249 supports submapper 0; canonical dumps use mapper 4:5");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported T9552 ROM geometry");
        if (header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
            return std::unexpected("T9552 supports CHR ROM without CHR RAM");
        if (header.is_nes2) {
            if ((header.prg_ram_size != 0U && header.prg_ram_size != 0x2000U) ||
                (header.prg_nvram_size != 0U && header.prg_nvram_size != 0x2000U) ||
                (header.prg_ram_size != 0U && header.prg_nvram_size != 0U))
                return std::unexpected("T9552 supports one optional 8 KiB WRAM chip");
            if (header.has_battery != (header.prg_nvram_size != 0U))
                return std::unexpected("T9552 battery flag and NVRAM declaration disagree");
        }
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("T9552 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 123U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 123 supports submapper 0");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-123 ROM geometry");
        // Support the documented ROM-only profile, without inferring WRAM
        // from the legacy iNES header's default allocation.
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            header.prg_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
            return std::unexpected("Mapper 123 supported profile requires PRG/CHR ROM without RAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 123 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 114U) {
        if (header.is_nes2 && header.submapper > 1U)
            return std::unexpected("Mapper 114 supports submappers 0 and 1");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x80000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-114 ROM geometry");
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            header.prg_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
            return std::unexpected("Mapper 114 supported profile requires PRG/CHR ROM without RAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 114 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 115U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Mapper 115 supports submapper 0");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x80000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-115 ROM geometry");
        // The documented control-register profile establishes ROM banking,
        // not an alternate WRAM population. Do not infer RAM from legacy defaults.
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            header.prg_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
            return std::unexpected("Mapper 115 supported profile requires PRG/CHR ROM without RAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 115 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 250U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected(
                "Mapper 250 supports submapper 0; use an explicit board profile");
        if (prg_size < 0x8000U || prg_size > 0x20000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x20000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-250 ROM geometry");
        // Both documented PCBs contain no WRAM. Ignore only legacy iNES's
        // implicit CPU-RAM size, never an explicit NES 2.0 declaration.
        if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
            header.prg_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
            return std::unexpected("Mapper 250 PCBs require ROM-only cartridge memory");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 250 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 263U) {
        if (!header.is_nes2 || header.submapper != 0U)
            return std::unexpected("Mapper 263 requires NES 2.0 submapper 0");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-263 ROM geometry");
        if (header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
            return std::unexpected("Mapper 263 supports CHR ROM without CHR RAM");
        if (header.prg_ram_size > 0x2000U || header.prg_nvram_size > 0x2000U ||
            header.prg_ram_size + header.prg_nvram_size > 0x2000U)
            return std::unexpected("Mapper 263 WRAM exceeds 8 KiB");
        const auto ram = header.prg_ram_size + header.prg_nvram_size;
        if (ram != 0U && (ram & (ram - 1U)) != 0U)
            return std::unexpected("Mapper 263 WRAM size must be a power of two");
        if (header.has_battery != (header.prg_nvram_size != 0U))
            return std::unexpected("Mapper 263 battery flag and NVRAM declaration disagree");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 263 supports ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 262U) {
        if (!header.is_nes2 || header.submapper != 0U)
            return std::unexpected("Mapper 262 requires NES 2.0 submapper 0");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x80000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-262 ROM geometry");
        // Its fixed four-screen VRAM is excluded from the NES 2.0 CHR-RAM
        // field. Only the 8 KiB pattern RAM is counted. No WRAM population
        // is established by the source for this supported board profile.
        if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
            header.chr_nvram_size != 0U ||
            (header.chr_ram_size != 0U && header.chr_ram_size != 0x2000U))
            return std::unexpected("Mapper 262 requires 8 KiB volatile CHR RAM and no WRAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical &&
            header.mirror_mode != MirrorMode::FourScreen)
            return std::unexpected("Unsupported mapper-262 header mirroring");
        return {};
    }
    if (header.mapper_number == 513U) {
        if (!header.is_nes2 || header.submapper != 0U)
            return std::unexpected("Mapper 513 requires NES 2.0 submapper 0");
        if (prg_size < 0x80000U || prg_size > 0x200000U || prg_size % 0x80000U != 0U ||
            chr_size != 0U)
            return std::unexpected("Mapper 513 requires 512 KiB outer PRG units and no CHR ROM");
        if (!header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
            header.chr_ram_size != 0U ||
            (header.chr_nvram_size != 0U && header.chr_nvram_size != 0x8000U))
            return std::unexpected("Mapper 513 requires 32 KiB battery CHR RAM and no WRAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Mapper 513 provides ordinary H/V CIRAM");
        return {};
    }
    if (header.mapper_number == 512U) {
        if (!header.is_nes2 || header.submapper != 0U)
            return std::unexpected("Mapper 512 requires NES 2.0 submapper 0");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size < 0x2000U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported mapper-512 ROM geometry");
        // Its mandatory PPU memory is 4 KiB pattern RAM plus 4 KiB VRAM.
        // NES 2.0 accounts for both in the CHR RAM field. Zero declarations
        // use the board's physical defaults; contradictory sizes are rejected.
        if (!header.has_battery || header.prg_ram_size != 0U ||
            (header.prg_nvram_size != 0U && header.prg_nvram_size != 0x2000U) ||
            (header.chr_ram_size != 0U && header.chr_ram_size != 0x2000U) ||
            header.chr_nvram_size != 0U)
            return std::unexpected(
                "Mapper 512 requires 8 KiB battery WRAM and 8 KiB volatile PPU RAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical &&
            header.mirror_mode != MirrorMode::FourScreen)
            return std::unexpected("Unsupported mapper-512 header mirroring");
        return {};
    }
    if (header.mapper_number == 157U)
        return Datach::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number == 16U || header.mapper_number == 153U || header.mapper_number == 159U)
        return BandaiFcg::validate_geometry(header, prg_size, chr_size);
    if (header.mapper_number != 31 && prg_size < 16 * 1024) {
        return std::unexpected("Mapper requires at least a 16 KiB PRG ROM");
    }
    if (header.mapper_number == 0 && prg_size != 16 * 1024 && prg_size != 32 * 1024) {
        return std::unexpected("NROM requires a 16 KiB or 32 KiB PRG ROM");
    }
    if (header.mapper_number == 4 && prg_size > 512 * 1024) {
        return std::unexpected("MMC3 PRG ROM exceeds 512 KiB");
    }
    if (header.mapper_number == 4 && chr_size > 256 * 1024) {
        return std::unexpected("MMC3 CHR ROM exceeds 256 KiB");
    }
    if (header.mapper_number == 4 && chr_size > 0 && chr_size < 1024) {
        return std::unexpected("MMC3 CHR ROM is smaller than one bank");
    }
    if (header.mapper_number == 118U || header.mapper_number == 119U) {
        const bool tqrom = header.mapper_number == 119U;
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Unsupported MMC3 variant submapper");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size == 0U || chr_size > (tqrom ? 0x20000U : 0x40000U) || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported MMC3 variant ROM geometry");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("MMC3 variant does not provide four-screen VRAM");
        if (header.chr_nvram_size != 0U ||
            (header.chr_ram_size != 0U && (!tqrom || header.chr_ram_size != 0x2000U)))
            return std::unexpected("Unsupported MMC3 variant CHR RAM geometry");
        if (tqrom) {
            if ((header.is_nes2 && header.prg_ram_size != 0U) || header.prg_nvram_size != 0U ||
                header.has_battery)
                return std::unexpected("TQROM does not provide PRG RAM or NVRAM");
        } else {
            if (header.prg_ram_size > 0x2000U || header.prg_nvram_size > 0x2000U ||
                header.prg_ram_size + header.prg_nvram_size > 0x2000U)
                return std::unexpected("TxSROM PRG RAM exceeds 8 KiB");
            const auto ram_size = header.prg_ram_size + header.prg_nvram_size;
            if (ram_size != 0U && (ram_size & (ram_size - 1U)) != 0U)
                return std::unexpected("TxSROM RAM size must be a power of two");
            if (header.is_nes2 && header.has_battery != (header.prg_nvram_size != 0U))
                return std::unexpected("TxSROM battery flag and NVRAM declaration disagree");
        }
    }
    if (header.mapper_number == 64U || header.mapper_number == 158U) {
        if (header.is_nes2 && header.submapper != 0U)
            return std::unexpected("Unsupported RAMBO-1 submapper");
        if (prg_size < 0x8000U || prg_size > 0x40000U || prg_size % 0x2000U != 0U ||
            chr_size == 0U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported RAMBO-1 ROM geometry");
        if ((header.is_nes2 && header.prg_ram_size != 0U) || header.prg_nvram_size != 0U ||
            header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
            return std::unexpected("RAMBO-1 does not provide RAM or NVRAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("RAMBO-1 does not provide four-screen VRAM");
    }
    if (header.mapper_number == 3 && chr_size > 0 && chr_size < 8 * 1024) {
        return std::unexpected("CNROM CHR ROM is smaller than one bank");
    }
    if (header.mapper_number == 7 && prg_size < 32 * 1024) {
        return std::unexpected("AxROM requires at least a 32 KiB PRG ROM");
    }
    if (header.mapper_number == 9 && prg_size < 32 * 1024) {
        return std::unexpected("MMC2 requires at least a 32 KiB PRG ROM");
    }
    switch (header.mapper_number) {
    case 10:
    case 21:
    case 22:
    case 23:
    case 25:
    case 75:
    case 76:
    case 88:
    case 95:
    case 154:
    case 193:
    case 206:
        if (prg_size < 32 * 1024)
            return std::unexpected("Mapper requires at least a 32 KiB PRG ROM");
        break;
    case 13:
        if (chr_size != 0)
            return std::unexpected("CPROM requires CHR RAM, not CHR ROM");
        break;
    default:
        break;
    }
    if ((header.mapper_number == 21 || header.mapper_number == 22 || header.mapper_number == 23 ||
         header.mapper_number == 25 || header.mapper_number == 73 || header.mapper_number == 75) &&
        (header.prg_ram_size > 8192 || header.prg_nvram_size > 8192 ||
         header.prg_ram_size + header.prg_nvram_size > 8192)) {
        return std::unexpected("VRC board PRG RAM exceeds 8 KiB");
    }
    if (header.is_nes2) {
        if ((header.mapper_number == 32 && header.submapper > 1) ||
            (header.mapper_number == 65 && header.submapper != 0))
            return std::unexpected("Unsupported Irem submapper");
        if (header.mapper_number == 21 && header.submapper > 2)
            return std::unexpected("Unsupported VRC4 submapper");
        if ((header.mapper_number == 23 || header.mapper_number == 25) && header.submapper > 3)
            return std::unexpected("Unsupported VRC2/VRC4 submapper");
        if (header.mapper_number == 34 && header.submapper > 2)
            return std::unexpected("Unsupported mapper 34 submapper");
        if (header.mapper_number == 71 && header.submapper > 1)
            return std::unexpected("Unsupported mapper 71 submapper");
        if (header.mapper_number == 78 && header.submapper != 0 && header.submapper != 1 &&
            header.submapper != 3)
            return std::unexpected("Unsupported mapper 78 submapper");
        if ((header.mapper_number == 61 || header.mapper_number == 200 ||
             header.mapper_number == 206 || header.mapper_number == 232) &&
            header.submapper > 1)
            return std::unexpected("Unsupported board submapper");
    }
    if (header.mapper_number == 32 || header.mapper_number == 65) {
        const std::size_t max_prg = header.mapper_number == 32 ? 0x40000U : 0x80000U;
        if (prg_size < 0x8000U || prg_size > max_prg || prg_size % 0x2000U != 0U ||
            chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported Irem ROM geometry");
        if (header.prg_ram_size > 0x2000U || header.prg_nvram_size > 0x2000U ||
            header.prg_ram_size + header.prg_nvram_size > 0x2000U ||
            header.chr_ram_size > 0x2000U || header.chr_nvram_size != 0U)
            return std::unexpected("Unsupported Irem RAM geometry");
    }
    if (header.mapper_number == 67 || header.mapper_number == 68) {
        const bool sunsoft4 = header.mapper_number == 68;
        const bool dcs = sunsoft4 && header.is_nes2 && header.submapper == 1;
        if (header.is_nes2 && header.submapper > (sunsoft4 ? 1 : 0))
            return std::unexpected("Unsupported Sunsoft-3/4 submapper");
        if (prg_size < 0x8000U || prg_size > (dcs ? 0x20000U : 0x40000U) ||
            prg_size % 0x4000U != 0U || chr_size > (sunsoft4 ? 0x40000U : 0x20000U) ||
            chr_size % 0x800U != 0U || (sunsoft4 && chr_size == 0U))
            return std::unexpected("Unsupported Sunsoft-3/4 ROM geometry");
        if (header.prg_ram_size > 0x2000U || header.prg_nvram_size > 0x2000U ||
            header.prg_ram_size + header.prg_nvram_size > 0x2000U ||
            header.chr_ram_size > 0x2000U || header.chr_nvram_size != 0U)
            return std::unexpected("Unsupported Sunsoft-3/4 RAM geometry");
    }
    if (header.mapper_number == 33 || header.mapper_number == 48) {
        if (header.is_nes2 && header.submapper > (header.mapper_number == 48 ? 1 : 0))
            return std::unexpected("Unsupported Taito TC submapper");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size > 0x80000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported Taito TC ROM geometry");
        if ((header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)) ||
            header.chr_ram_size > 0x2000U || header.chr_nvram_size != 0U ||
            (chr_size != 0U && header.chr_ram_size != 0U))
            return std::unexpected("Unsupported Taito TC RAM geometry");
    }
    if (header.mapper_number == 80 || header.mapper_number == 207) {
        if (header.is_nes2 && header.submapper != 0)
            return std::unexpected("Unsupported X1-005 submapper");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported X1-005 ROM geometry");
        if (header.is_nes2 && ((header.prg_ram_size != 0U && header.prg_ram_size != 128U) ||
                               (header.prg_nvram_size != 0U && header.prg_nvram_size != 128U) ||
                               header.prg_ram_size + header.prg_nvram_size > 128U))
            return std::unexpected("X1-005 has only 128 bytes of internal PRG memory");
        if (header.is_nes2 && header.prg_nvram_size != 0U && !header.has_battery)
            return std::unexpected("X1-005 NVRAM requires the battery flag");
        if (header.chr_ram_size > 0x2000U || header.chr_nvram_size != 0U ||
            (chr_size != 0U && header.chr_ram_size != 0U))
            return std::unexpected("Unsupported X1-005 CHR RAM geometry");
    }
    if (header.mapper_number == 210) {
        if (header.is_nes2 && header.submapper > 2U)
            return std::unexpected("Unsupported Namco 175/340 submapper");
        const bool namco175 = header.is_nes2
                                  ? (header.submapper == 1U ||
                                     (header.submapper == 0U &&
                                      (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
                                  : header.has_battery;
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size == 0U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported Namco 175/340 ROM geometry");
        if (header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
            return std::unexpected("Namco 175/340 requires CHR ROM");
        if (namco175) {
            if (header.prg_ram_size > 0x2000U || header.prg_nvram_size > 0x2000U ||
                header.prg_ram_size + header.prg_nvram_size > 0x2000U)
                return std::unexpected("Namco 175 PRG RAM exceeds 8 KiB");
            const auto size = header.prg_ram_size + header.prg_nvram_size;
            if (size != 0U && (size & (size - 1U)) != 0U)
                return std::unexpected("Namco 175 RAM size must be a power of two");
            if (header.prg_nvram_size != 0U && !header.has_battery)
                return std::unexpected("Namco 175 NVRAM requires the battery flag");
            if (header.is_nes2 && header.has_battery && header.prg_nvram_size == 0U)
                return std::unexpected("Namco 175 battery flag requires declared NVRAM");
        } else if ((header.is_nes2 && header.prg_ram_size != 0U) || header.prg_nvram_size != 0U ||
                   header.has_battery) {
            return std::unexpected("Namco 340 does not provide RAM or NVRAM");
        }
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Namco 175/340 requires an H/V startup arrangement");
    }
    if (header.mapper_number == 77) {
        if (header.is_nes2 && header.submapper != 0)
            return std::unexpected("Unsupported LROG017 submapper");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x8000U != 0U ||
            chr_size == 0U || chr_size > 0x8000U || chr_size % 0x800U != 0U)
            return std::unexpected("Unsupported LROG017 ROM geometry");
        // Its mandatory 8 KiB SRAM supplies 6 KiB of pattern RAM and 2 KiB
        // of nametable RAM. NES 2.0 cannot encode a 6 KiB pattern portion.
        if ((header.is_nes2 && header.prg_ram_size != 0U) || header.has_battery ||
            header.prg_nvram_size != 0U || header.chr_nvram_size != 0U ||
            (header.chr_ram_size != 0U && header.chr_ram_size != 0x1800U &&
             header.chr_ram_size != 0x2000U))
            return std::unexpected("Unsupported LROG017 RAM geometry");
    }
    if (header.mapper_number == 72 || header.mapper_number == 92 || header.mapper_number == 101) {
        const bool chr_only = header.mapper_number == 101;
        if (header.is_nes2 && header.submapper != 0)
            return std::unexpected("Unsupported Jaleco discrete submapper");
        const std::size_t max_prg =
            chr_only ? 0x8000U : (header.mapper_number == 72 ? 0x20000U : 0x40000U);
        const std::size_t min_prg = chr_only ? 0x4000U : 0x8000U;
        const std::size_t max_chr = chr_only ? 0x200000U : 0x20000U;
        if (prg_size < min_prg || prg_size > max_prg || prg_size % 0x4000U != 0U ||
            chr_size == 0U || chr_size > max_chr || chr_size % 0x2000U != 0U)
            return std::unexpected("Unsupported Jaleco discrete ROM geometry");
        if ((header.is_nes2 && header.prg_ram_size != 0U) || header.has_battery ||
            header.prg_nvram_size != 0U || header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
            return std::unexpected("Jaleco discrete boards do not provide RAM or NVRAM");
        if (header.mirror_mode != MirrorMode::Horizontal &&
            header.mirror_mode != MirrorMode::Vertical)
            return std::unexpected("Jaleco discrete boards require fixed H/V mirroring");
    }
    if (header.mapper_number == 18) {
        if (header.is_nes2 && header.submapper != 0)
            return std::unexpected("Unsupported SS88006 submapper");
        if (prg_size < 0x8000U || prg_size > 0x80000U || prg_size % 0x2000U != 0U ||
            chr_size == 0U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported SS88006 ROM geometry");
        if (header.prg_ram_size > 0x2000U || header.prg_nvram_size > 0x2000U ||
            header.prg_ram_size + header.prg_nvram_size > 0x2000U || header.chr_ram_size != 0U ||
            header.chr_nvram_size != 0U)
            return std::unexpected("Unsupported SS88006 RAM geometry");
        if (header.prg_nvram_size != 0U && !header.has_battery)
            return std::unexpected("SS88006 NVRAM requires the battery flag");
        if (header.mirror_mode == MirrorMode::FourScreen)
            return std::unexpected("SS88006 does not support four-screen nametables");
    }
    if (header.mapper_number == 82 || header.mapper_number == 552) {
        if (header.is_nes2 && header.submapper != 0)
            return std::unexpected("Unsupported X1-017 submapper");
        const std::size_t max_prg = header.mapper_number == 82 ? 0x20000U : 0x80000U;
        if (prg_size < 0x8000U || prg_size > max_prg || prg_size % 0x2000U != 0U ||
            chr_size == 0U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
            return std::unexpected("Unsupported X1-017 ROM geometry");
        // Mandatory internal 5 KiB cannot be represented by NES 2.0's RAM
        // exponent. Accept bounded declarations (including rounded-up 8 KiB),
        // but never allocate external RAM or enlarge the physical window.
        if (header.prg_ram_size > 0x2000U || header.prg_nvram_size > 0x2000U ||
            header.prg_ram_size + header.prg_nvram_size > 0x2000U || header.chr_ram_size != 0U ||
            header.chr_nvram_size != 0U)
            return std::unexpected("Unsupported X1-017 RAM geometry");
        if (header.is_nes2 && header.prg_nvram_size != 0U && !header.has_battery)
            return std::unexpected("X1-017 NVRAM requires the battery flag");
    }
    return {};
}

} // namespace

MapperRegistry& MapperRegistry::instance() {
    // Readers retaining ownership may continue during static teardown. The
    // cached reference does not access the static shared_ptr after its lifetime.
    static auto& registry = *retain();
    return registry;
}

std::shared_ptr<MapperRegistry> MapperRegistry::retain() {
    struct SharedRegistry final : MapperRegistry {};
    static const auto registry = std::make_shared<SharedRegistry>();
    return registry;
}

void MapperRegistry::register_mapper(uint16_t mapper_number, MapperFactory factory) {
    factories_[mapper_number] = std::move(factory);
}

Result<std::unique_ptr<Mapper>> MapperRegistry::create(const INesHeader& header,
                                                       std::vector<Byte> prg_rom,
                                                       std::vector<Byte> chr_rom) const {
    auto it = factories_.find(header.mapper_number);
    if (it == factories_.end()) {
        return std::unexpected("Unsupported mapper: " + std::to_string(header.mapper_number));
    }
    if (auto valid = validate_geometry(header, prg_rom.size(), chr_rom.size()); !valid) {
        return std::unexpected(valid.error());
    }
    return it->second(header, std::move(prg_rom), std::move(chr_rom));
}

namespace {

template <typename T> MapperFactory make_factory() {
    return [](const INesHeader& header,
              std::vector<Byte> prg_rom,
              std::vector<Byte> chr_rom) -> std::unique_ptr<Mapper> {
        return std::make_unique<T>(header, std::move(prg_rom), std::move(chr_rom));
    };
}

} // namespace

void register_builtin_mappers() {
    auto& registry = MapperRegistry::instance();
    for (const uint16_t id : {133U, 135U, 137U, 138U, 139U, 141U, 143U, 146U, 150U, 243U})
        registry.register_mapper(id, make_factory<Sachen>());
    for (const uint16_t id : {36U, 132U, 136U, 147U, 172U, 173U})
        registry.register_mapper(id, make_factory<Txc>());
    for (const uint16_t id :
         {35U, 90U, 160U, 209U, 211U, 281U, 282U, 295U, 358U, 386U, 387U, 388U, 397U})
        registry.register_mapper(id, make_factory<JyCompany>());
    registry.register_mapper(0, make_factory<Nrom>());
    registry.register_mapper(1, make_factory<Mmc1>());
    registry.register_mapper(2, make_factory<Uxrom>());
    registry.register_mapper(3, make_factory<Cnrom>());
    registry.register_mapper(4, make_factory<Mmc3>());
    registry.register_mapper(114, make_factory<Mmc3>());
    registry.register_mapper(115, make_factory<Mmc3>());
    registry.register_mapper(123, make_factory<Mmc3>());
    registry.register_mapper(118, make_factory<Mmc3>());
    registry.register_mapper(119, make_factory<Mmc3>());
    registry.register_mapper(74, make_factory<Mmc3>());
    registry.register_mapper(192, make_factory<Mmc3>());
    registry.register_mapper(194, make_factory<Mmc3>());
    registry.register_mapper(195, make_factory<Mmc3>());
    registry.register_mapper(196, make_factory<Mmc3>());
    registry.register_mapper(197, make_factory<Mmc3>());
    registry.register_mapper(198, make_factory<Mmc3>());
    registry.register_mapper(199, make_factory<Mmc3>());
    registry.register_mapper(205, make_factory<Mmc3>());
    registry.register_mapper(208, make_factory<Mmc3>());
    registry.register_mapper(215, make_factory<Mmc3>());
    registry.register_mapper(218, make_factory<SingleChip>());
    registry.register_mapper(219, make_factory<Mmc3>());
    registry.register_mapper(221, make_factory<NtdecN625092>());
    registry.register_mapper(227, make_factory<Multicart227>());
    registry.register_mapper(228, make_factory<Action52>());
    registry.register_mapper(230, make_factory<Multicart230>());
    registry.register_mapper(233, make_factory<Multicart233>());
    registry.register_mapper(234, make_factory<Maxi15>());
    registry.register_mapper(235, make_factory<GoldenGame>());
    registry.register_mapper(236, make_factory<RealtecMulticart>());
    registry.register_mapper(237, make_factory<Teletubbies>());
    registry.register_mapper(238, make_factory<Mmc3>());
    registry.register_mapper(241, make_factory<BxromWram>());
    registry.register_mapper(242, make_factory<Multicart242>());
    registry.register_mapper(244, make_factory<Decathlon>());
    registry.register_mapper(245, make_factory<Mmc3>());
    registry.register_mapper(246, make_factory<G0151>());
    registry.register_mapper(249, make_factory<Mmc3>());
    registry.register_mapper(250, make_factory<Mmc3>());
    registry.register_mapper(252, make_factory<KonamiVrc>());
    registry.register_mapper(253, make_factory<KonamiVrc>());
    registry.register_mapper(254, make_factory<Mmc3>());
    registry.register_mapper(262, make_factory<Mmc3>());
    registry.register_mapper(263, make_factory<Mmc3>());
    registry.register_mapper(512, make_factory<Mmc3>());
    registry.register_mapper(513, make_factory<Mmc3>());
    registry.register_mapper(64, make_factory<Rambo1>());
    registry.register_mapper(158, make_factory<Rambo1>());
    registry.register_mapper(16, make_factory<BandaiFcg>());
    registry.register_mapper(153, make_factory<BandaiFcg>());
    registry.register_mapper(159, make_factory<BandaiFcg>());
    registry.register_mapper(157, make_factory<Datach>());
    registry.register_mapper(30, make_factory<FlashBoards>());
    registry.register_mapper(111,
                             [](const INesHeader& h,
                                std::vector<Byte> prg,
                                std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
                                 if (!chr.empty())
                                     return std::make_unique<Mmc1>(
                                         h, std::move(prg), std::move(chr));
                                 return std::make_unique<FlashBoards>(h, std::move(prg), chr);
                             });
    registry.register_mapper(7, make_factory<Axrom>());
    registry.register_mapper(5, make_factory<Mmc5>());
    registry.register_mapper(9, make_factory<Mmc2>());
    registry.register_mapper(11, make_factory<ColorDreams>());
    registry.register_mapper(19, make_factory<Namco163>());
    registry.register_mapper(24, make_factory<Vrc6>());
    registry.register_mapper(26, make_factory<Vrc6>());
    registry.register_mapper(69, make_factory<Sunsoft5b>());
    registry.register_mapper(85, make_factory<Vrc7>());
    registry.register_mapper(10, make_factory<Mmc4>());
    registry.register_mapper(28, make_factory<DiscreteMapper>());
    registry.register_mapper(31, make_factory<DiscreteMapper>());
    registry.register_mapper(32, make_factory<Irem>());
    registry.register_mapper(65, make_factory<Irem>());
    registry.register_mapper(67, make_factory<Sunsoft34>());
    registry.register_mapper(68, make_factory<Sunsoft34>());
    registry.register_mapper(33, make_factory<TaitoTc>());
    registry.register_mapper(48, make_factory<TaitoTc>());
    registry.register_mapper(80, make_factory<TaitoX1005>());
    registry.register_mapper(207, make_factory<TaitoX1005>());
    registry.register_mapper(82, make_factory<TaitoX1017>());
    registry.register_mapper(552, make_factory<TaitoX1017>());
    registry.register_mapper(18, make_factory<JalecoSs88006>());
    registry.register_mapper(72, make_factory<JalecoDiscrete>());
    registry.register_mapper(92, make_factory<JalecoDiscrete>());
    registry.register_mapper(101, make_factory<JalecoDiscrete>());
    registry.register_mapper(77, make_factory<IremLrog017>());
    registry.register_mapper(210, make_factory<Namco175340>());
    for (const uint16_t id : {21, 22, 23, 25, 73, 75})
        registry.register_mapper(id, make_factory<KonamiVrc>());
    for (const uint16_t id :
         {13, 34, 66, 70, 71, 78, 79, 87, 89, 93, 94, 97, 113, 140, 152, 180, 184}) {
        registry.register_mapper(id, make_factory<DiscreteMapper>());
    }
    for (const uint16_t id : {76, 88, 95, 154, 206}) {
        registry.register_mapper(id, make_factory<Namco108>());
    }
    for (const uint16_t id : {38,  46,  58,  61,  62,  81,  107, 145, 148, 149, 193, 200,
                              201, 202, 203, 212, 213, 225, 226, 229, 231, 232, 240}) {
        registry.register_mapper(id, make_factory<DiscreteMapper>());
    }
    for (const uint16_t id : {6U, 8U})
        registry.register_mapper(id, make_factory<FfeMagicCard>());
    registry.register_mapper(27, make_factory<Mapper027>());
    registry.register_mapper(42, make_factory<Mapper042>());
    registry.register_mapper(43, make_factory<Mapper043>());
    registry.register_mapper(44, make_factory<Mapper044>());
    registry.register_mapper(45, make_factory<Mapper045>());
    registry.register_mapper(47, make_factory<Mmc3Multicart47>());
    registry.register_mapper(50, make_factory<N32Mapper050>());
    registry.register_mapper(52, make_factory<Realtec8213>());
    registry.register_mapper(55, make_factory<Mario1Malee2>());
    registry.register_mapper(59, make_factory<BmcT3h53>());
    registry.register_mapper(60, make_factory<Multicart060>());
    registry.register_mapper(63, make_factory<Multicart063>());
    for (const uint16_t id : {83U, 264U})
        registry.register_mapper(id, make_factory<ConyYoko>());
    registry.register_mapper(96, make_factory<OekaKids>());
    registry.register_mapper(105, make_factory<NesEvent>());
    registry.register_mapper(108, make_factory<FdsConversion108>());
    registry.register_mapper(112, make_factory<Mapper112>());
    registry.register_mapper(120, make_factory<Lh15>());
    registry.register_mapper(122, make_factory<Jy043>());
    registry.register_mapper(134, make_factory<T4a54a>());
    registry.register_mapper(142, make_factory<KaiserKs202>());
    registry.register_mapper(164, make_factory<Fs304>());
    for (const uint16_t id : {166U, 167U})
        registry.register_mapper(id, make_factory<Mapper166>());
    registry.register_mapper(174, make_factory<Ntdec174>());
    registry.register_mapper(183, make_factory<Mapper183>());
    registry.register_mapper(189, make_factory<Txc189>());
    registry.register_mapper(190, make_factory<Mapper190>());
    registry.register_mapper(191, make_factory<Mapper191>());
    registry.register_mapper(261, make_factory<Ntdec2746>());
    registry.register_mapper(274, make_factory<CartridgeStory274>());
    registry.register_mapper(283, make_factory<BlockFamily283>());
    registry.register_mapper(12, make_factory<Mapper012>());
    registry.register_mapper(14, make_factory<Sl1632>());
    registry.register_mapper(15, make_factory<Mapper015>());
    registry.register_mapper(17, make_factory<FfeSuperMagicCard>());
    registry.register_mapper(29, make_factory<RetCufrom>());
    registry.register_mapper(37, make_factory<Mmc3Multicart37>());
    registry.register_mapper(40, make_factory<Ntdec2722>());
    registry.register_mapper(41, make_factory<Caltron041>());
    registry.register_mapper(49, make_factory<Mapper049>());
    registry.register_mapper(56, make_factory<Mapper056Ks202>());
    registry.register_mapper(57, make_factory<Mapper057>());
    registry.register_mapper(86, make_factory<JalecoJf13>());
    registry.register_mapper(91, make_factory<Mapper091>());
    registry.register_mapper(103, make_factory<Mapper103>());
    registry.register_mapper(104, make_factory<Mapper104GoldenFive>());
    registry.register_mapper(106, make_factory<Mapper106>());
    registry.register_mapper(116, make_factory<SomariP116>());
    registry.register_mapper(117, make_factory<Mapper117>());
    registry.register_mapper(121, make_factory<Mapper121>());
    registry.register_mapper(124, make_factory<SuperGameMegaTypeIII>());
    registry.register_mapper(125, make_factory<Lh32>());
    registry.register_mapper(126, make_factory<Tec9719>());
    for (const uint16_t id : {128U, 265U})
        registry.register_mapper(id, make_factory<Mapper128>());
    registry.register_mapper(155, make_factory<Mmc1a>());
    registry.register_mapper(144, make_factory<Mapper144>());
    registry.register_mapper(156, make_factory<DaouMapper156>());
    registry.register_mapper(162, make_factory<Mapper162Fs304>());
    registry.register_mapper(163, make_factory<NanjingFc001>());
    registry.register_mapper(165, make_factory<Mapper165>());
    registry.register_mapper(168, make_factory<Racermate168>());
    registry.register_mapper(176, make_factory<Fk23c>());
    registry.register_mapper(177, make_factory<Henggedianzi177>());
    registry.register_mapper(185, make_factory<Cnrom185>());
    registry.register_mapper(187, make_factory<Mapper187>());
    registry.register_mapper(188, make_factory<KaraokeStudio188>());
    registry.register_mapper(224, make_factory<Kt008>());
    registry.register_mapper(257, make_factory<Mapper257>());
    registry.register_mapper(260, make_factory<HpMulticart260>());
    registry.register_mapper(266, make_factory<CityFighter266>());
    registry.register_mapper(267, make_factory<Mapper267>());
    registry.register_mapper(268, make_factory<Coolboy>());
    registry.register_mapper(269, make_factory<Mapper269>());
    registry.register_mapper(271, make_factory<Bmc22026>());
    registry.register_mapper(272, make_factory<Mapper272>());
    registry.register_mapper(273, make_factory<Mapper273>());
    registry.register_mapper(285, make_factory<Mapper285>());
    registry.register_mapper(286, make_factory<BenshiengBs5>());
    registry.register_mapper(287, make_factory<Mapper287>());
    registry.register_mapper(288, make_factory<Mapper288>());
    registry.register_mapper(289, make_factory<Mapper289>());
    registry.register_mapper(290, make_factory<Mapper290>());
    registry.register_mapper(291, make_factory<Mapper291>());
    registry.register_mapper(292, make_factory<Mapper292>());
    registry.register_mapper(293, make_factory<Mapper293>());
    registry.register_mapper(294, make_factory<Mapper294>());
    registry.register_mapper(284, make_factory<Drip>());
    registry.register_mapper(298, make_factory<UnlTf1201>());
    registry.register_mapper(299, make_factory<Mapper299>());
    registry.register_mapper(300, make_factory<Mapper300>());
    registry.register_mapper(302, make_factory<KaiserKs7057>());
    registry.register_mapper(304, make_factory<Mapper304>());
    registry.register_mapper(305, make_factory<Mapper305>());
    registry.register_mapper(306, make_factory<Mapper306>());
    registry.register_mapper(307, make_factory<Mapper307>());
    registry.register_mapper(308, make_factory<Mapper308>());
    registry.register_mapper(309, make_factory<Mapper309>());
    registry.register_mapper(310, make_factory<Mapper310>());
    registry.register_mapper(312, make_factory<Mapper312>());
    registry.register_mapper(313, make_factory<Mapper313>());
    registry.register_mapper(314, make_factory<Mapper314>());
    registry.register_mapper(315, make_factory<Mapper315>());
    registry.register_mapper(319, make_factory<Mapper319>());
    registry.register_mapper(320, make_factory<Mapper320>());
    registry.register_mapper(321, make_factory<Mapper321>());
    registry.register_mapper(322, make_factory<Mapper322>());
    registry.register_mapper(323, make_factory<FaridSlrom323>());
    registry.register_mapper(324, make_factory<Mapper324>());
    registry.register_mapper(325, make_factory<Mapper325>());
    registry.register_mapper(326, make_factory<Mapper326>());
    registry.register_mapper(327, make_factory<Mapper327>());
    registry.register_mapper(328, make_factory<Mapper328>());
    registry.register_mapper(330, make_factory<Mapper330>());
    registry.register_mapper(331, make_factory<Mapper331>());
    registry.register_mapper(332, make_factory<Mapper332>());
    registry.register_mapper(333, make_factory<Mapper333>());
    registry.register_mapper(334, make_factory<Mapper334>());
    registry.register_mapper(335, make_factory<Mapper335>());
    registry.register_mapper(336, make_factory<Mapper336>());
    registry.register_mapper(337, make_factory<Mapper337>());
    registry.register_mapper(338, make_factory<Mapper338>());
    registry.register_mapper(339, make_factory<Mapper339>());
    registry.register_mapper(340, make_factory<Mapper340>());
    registry.register_mapper(341, make_factory<Mapper341>());
    registry.register_mapper(344, make_factory<Mapper344>());
    registry.register_mapper(345, make_factory<Mapper345>());
    registry.register_mapper(346, make_factory<Mapper346>());
    registry.register_mapper(347, make_factory<Mapper347>());
    registry.register_mapper(348, make_factory<Mapper348>());
    registry.register_mapper(349, make_factory<Mapper349>());
    registry.register_mapper(350, make_factory<Mapper350>());
    registry.register_mapper(351, make_factory<Mapper351>());
    registry.register_mapper(353, make_factory<Mapper353>());
    registry.register_mapper(354, make_factory<Mapper354>());
    registry.register_mapper(356, make_factory<Mapper356>());
    registry.register_mapper(357, make_factory<Mapper357>());
    registry.register_mapper(359, make_factory<Mapper359>());
    registry.register_mapper(360, make_factory<Mapper360>());
    registry.register_mapper(361, make_factory<Mapper361>());
    registry.register_mapper(362, make_factory<Mapper362>());
    registry.register_mapper(364, make_factory<Mapper364>());
    registry.register_mapper(366, make_factory<Mapper366>());
    registry.register_mapper(367, make_factory<Mapper367>());
    registry.register_mapper(368, make_factory<Mapper368>());
    registry.register_mapper(369, make_factory<Mapper369>());
    registry.register_mapper(370, make_factory<Mapper370>());
    registry.register_mapper(372, make_factory<Mapper372>());
    registry.register_mapper(373, make_factory<Mapper373>());
    registry.register_mapper(374, make_factory<Mapper374>());
    registry.register_mapper(375, make_factory<Mapper375>());
    registry.register_mapper(376, make_factory<Mapper376>());
    registry.register_mapper(377, make_factory<Mapper377>());
    registry.register_mapper(378, make_factory<Mapper378>());
    registry.register_mapper(379, make_factory<Mapper379>());
    registry.register_mapper(380, make_factory<Mapper380>());
    registry.register_mapper(381, make_factory<Mapper381>());
    registry.register_mapper(382, make_factory<Mapper382>());
    registry.register_mapper(383, make_factory<Mapper383>());
    registry.register_mapper(384, make_factory<Mapper384>());
    registry.register_mapper(385, make_factory<Mapper385>());
    registry.register_mapper(389, make_factory<Mapper389>());
    registry.register_mapper(391, make_factory<Mapper391>());
    registry.register_mapper(392, make_factory<Mapper392>());
    registry.register_mapper(393, make_factory<Mapper393>());
    registry.register_mapper(394, make_factory<Mapper394>());
    registry.register_mapper(395, make_factory<Mapper395>());
    registry.register_mapper(396, make_factory<Mapper396>());
    registry.register_mapper(398, make_factory<Mapper398>());
    registry.register_mapper(399, make_factory<Mapper399>());
    registry.register_mapper(400, make_factory<Mapper400>());
    registry.register_mapper(401, make_factory<Mapper401>());
    registry.register_mapper(402, make_factory<Mapper402>());
    registry.register_mapper(403, make_factory<Mapper403>());
    registry.register_mapper(404, make_factory<Mapper404>());
    registry.register_mapper(406, make_factory<Mapper406>());
    registry.register_mapper(408, make_factory<Mapper408>());
    registry.register_mapper(409, make_factory<Mapper409>());
    registry.register_mapper(410, make_factory<Mapper410>());
    registry.register_mapper(411, make_factory<Mapper411>());
    registry.register_mapper(412, make_factory<Mapper412>());
    registry.register_mapper(414, make_factory<Mapper414>());
    registry.register_mapper(415, make_factory<Mapper415>());
    registry.register_mapper(416, make_factory<Mapper416>());
    registry.register_mapper(417, make_factory<Mapper417>());
    registry.register_mapper(422, make_factory<Mapper422>());
}

} // namespace mapperbus::core
