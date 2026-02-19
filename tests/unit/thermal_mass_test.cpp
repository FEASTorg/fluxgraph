#include "fluxgraph/model/thermal_mass.hpp"
#include <cmath>
#include <gtest/gtest.h>

using namespace fluxgraph;

class ThermalMassTest : public ::testing::Test {
protected:
  void SetUp() override {
    ns = std::make_unique<SignalNamespace>();
    store = std::make_unique<SignalStore>();

    temp_id = ns->intern("model/temperature");
    power_id = ns->intern("model/heating_power");
    ambient_id = ns->intern("model/ambient_temp");
  }

  std::unique_ptr<SignalNamespace> ns;
  std::unique_ptr<SignalStore> store;
  SignalId temp_id, power_id, ambient_id;
};

TEST_F(ThermalMassTest, InitialTemperature) {
  ThermalMassModel model("test", 1000.0, 10.0, 25.0, "model/temperature",
                         "model/heating_power", "model/ambient_temp", *ns);

  store->write(power_id, 0.0, "W");
  store->write(ambient_id, 20.0, "degC");

  model.tick(0.1, *store);

  double temp = store->read_value(temp_id);
  EXPECT_NE(temp, 25.0); // Should start cooling
}

TEST_F(ThermalMassTest, HeatingBehavior) {
  ThermalMassModel model("test", 1000.0, 10.0, 20.0, "model/temperature",
                         "model/heating_power", "model/ambient_temp", *ns);

  store->write(power_id, 100.0, "W"); // Net heating
  store->write(ambient_id, 20.0, "degC");

  double initial_temp = 20.0;
  for (int i = 0; i < 10; ++i) {
    model.tick(0.1, *store);
  }

  double final_temp = store->read_value(temp_id);
  EXPECT_GT(final_temp, initial_temp); // Temperature should increase
}

TEST_F(ThermalMassTest, CoolingBehavior) {
  ThermalMassModel model("test", 1000.0, 10.0, 100.0, "model/temperature",
                         "model/heating_power", "model/ambient_temp", *ns);

  store->write(power_id, 0.0, "W"); // No heating
  store->write(ambient_id, 20.0, "degC");

  double initial_temp = 100.0;
  for (int i = 0; i < 100; ++i) {
    model.tick(0.1, *store);
  }

  double final_temp = store->read_value(temp_id);
  EXPECT_LT(final_temp, initial_temp); // Temperature should decrease
  EXPECT_GT(final_temp, 20.0);         // But not below ambient
}

TEST_F(ThermalMassTest, Equilibrium) {
  ThermalMassModel model("test", 1000.0, 10.0, 50.0, "model/temperature",
                         "model/heating_power", "model/ambient_temp", *ns);

  store->write(ambient_id, 20.0, "degC");

  // Run until equilibrium
  for (int i = 0; i < 10; ++i) {
    model.tick(1.0, *store);
    double temp = store->read_value(temp_id);
    double heat_loss = 10.0 * (temp - 20.0);
    store->write(power_id, heat_loss, "W"); // Match heat loss
  }

  double temp_before = store->read_value(temp_id);
  model.tick(1.0, *store);
  double temp_after = store->read_value(temp_id);

  EXPECT_NEAR(temp_before, temp_after, 0.1); // Stable
}

TEST_F(ThermalMassTest, Reset) {
  ThermalMassModel model("test", 1000.0, 10.0, 25.0, "model/temperature",
                         "model/heating_power", "model/ambient_temp", *ns);

  store->write(power_id, 1000.0, "W");
  store->write(ambient_id, 20.0, "degC");

  for (int i = 0; i < 10; ++i) {
    model.tick(0.1, *store);
  }

  double temp_heated = store->read_value(temp_id);
  EXPECT_GT(temp_heated, 25.0);

  model.reset();
  model.tick(0.0, *store); // Tick with dt=0 to update store

  double temp_reset = store->read_value(temp_id);
  EXPECT_NEAR(temp_reset, 25.0, 0.1);
}

TEST_F(ThermalMassTest, StabilityLimit) {
  ThermalMassModel model("test", 1000.0, 10.0, 25.0, "model/temperature",
                         "model/heating_power", "model/ambient_temp", *ns);

  double limit = model.compute_stability_limit();
  EXPECT_NEAR(limit, 200.0, 0.1); // 2*1000/10 = 200
}

TEST_F(ThermalMassTest, PhysicsDrivenFlag) {
  ThermalMassModel model("test", 1000.0, 10.0, 25.0, "model/temperature",
                         "model/heating_power", "model/ambient_temp", *ns);

  store->write(power_id, 0.0, "W");
  store->write(ambient_id, 20.0, "degC");

  model.tick(0.1, *store);

  EXPECT_TRUE(store->is_physics_driven(temp_id));
}

TEST_F(ThermalMassTest, Describe) {
  ThermalMassModel model("chamber_air", 8000.0, 50.0, 25.0, "model/temperature",
                         "model/heating_power", "model/ambient_temp", *ns);

  std::string desc = model.describe();
  EXPECT_NE(desc.find("ThermalMass"), std::string::npos);
  EXPECT_NE(desc.find("8000"), std::string::npos);
  EXPECT_NE(desc.find("50"), std::string::npos);
}
