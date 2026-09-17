#include "retroemu/core/gameboy.hpp"

#include "retroemu/core/cartridge.hpp"

namespace retroemu {
namespace {

// The header decides the model unless the caller forces it.
Model model_for(const Cartridge &cart, bool force_cgb)
{
    if (force_cgb) return Model::Cgb;
    return cart.header().cgb == CgbSupport::Only ? Model::Cgb : Model::Dmg;
}

}  // namespace

bool GameBoy::load(const std::string &path, bool force_cgb, std::string &error)
{
    Cartridge cart;
    if (!cart.load_from_file(path, error)) return false;

    model_ = model_for(cart, force_cgb);
    bus_.attach(std::move(cart), model_);
    cpu_.reset(model_);
    return true;
}

bool GameBoy::load_from_memory(std::vector<u8> rom, const std::string &name,
                               bool force_cgb, std::string &error)
{
    Cartridge cart;
    if (!cart.load_from_memory(std::move(rom), name, error)) return false;

    model_ = model_for(cart, force_cgb);
    bus_.attach(std::move(cart), model_);
    cpu_.reset(model_);
    return true;
}

void GameBoy::reset()
{
    bus_.reset();
    cpu_.reset(model_);
}

u32 GameBoy::step() { return cpu_.step(bus_); }

void GameBoy::run_system_cycles(u64 t_sys)
{
    const u64 target = bus_.clock().t_sys() + t_sys;
    while (bus_.clock().t_sys() < target) {
        step();
        if (cpu_.illegal() || cpu_.stopped()) break;
    }
}

void GameBoy::run_seconds(double seconds)
{
    run_system_cycles(static_cast<u64>(seconds * kSystemClockHz));
}

}  // namespace retroemu
