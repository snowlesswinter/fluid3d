#include "stdafx.h"
#include "metrics.h"

namespace
{
const size_t kMaxNumOfTimeStamps = 500;
const int kNumOfSamples = 20;
}

Metrics::Metrics()
    : time_stamps_()
    , last_operation_time_(0.0)
    , operation_time_costs_()
{
}

Metrics::~Metrics()
{
}

void Metrics::OnFrameRendered(float current_time)
{
    time_stamps_.push_front(current_time);
    while (time_stamps_.size() > kMaxNumOfTimeStamps)
        time_stamps_.pop_back();
}

float Metrics::GetFrameRate(float current_time) const
{
    if (time_stamps_.empty())
        return 0.0f;

    return time_stamps_.size() / (current_time - time_stamps_.back());
}

void Metrics::OnFrameBegins(double current_time)
{
    last_operation_time_ = current_time;
}

void Metrics::OnVelocityAvected(double current_time)
{
    OnOperationProceeded(AVECT_VELOCITY, current_time);
}

void Metrics::OnTemperatureAvected(double current_time)
{
    OnOperationProceeded(AVECT_TEMPERATURE, current_time);
}

void Metrics::OnDensityAvected(double current_time)
{
    OnOperationProceeded(AVECT_DENSITY, current_time);
}

void Metrics::OnBuoyancyApplied(double current_time)
{
    OnOperationProceeded(APPLY_BUOYANCY, current_time);
}

void Metrics::OnImpulseApplied(double current_time)
{
    OnOperationProceeded(APPLY_IMPULSE, current_time);
}

void Metrics::OnDivergenceComputed(double current_time)
{
    OnOperationProceeded(COMPUTE_DIVERGENCE, current_time);
}

void Metrics::OnPressureSolved(double current_time)
{
    OnOperationProceeded(SOLVE_PRESSURE, current_time);
}

void Metrics::OnVelocityRectified(double current_time)
{
    OnOperationProceeded(RECTIFY_VELOCITY, current_time);
}

void Metrics::OnOperationProceeded(Operations o, double current_time)
{
    auto& samples = operation_time_costs_[o];

    // Store in microseconds.
    samples.push_front((current_time - last_operation_time_) * 1000000.0);
    while (samples.size() > kNumOfSamples)
        samples.pop_back();

    last_operation_time_ = current_time;
}
