#include "AxisInputShaper.h"
#include "MoveQueue.h"
#include "../../../../snapmaker/debug/debug.h"

void ShaperWindow::updateParamABC(int i, float start_v, float accelerate, time_double_t start_t, time_double_t left_time, float start_pos, float axis_r) {
    ShaperWindowParams &p = params[i];
    float A = p.A;
    float d_t = (start_t - left_time);
    float T = p.T - d_t;
    p.a = 0.5f * accelerate * A * axis_r;
    p.b = (start_v + accelerate * T) * A * axis_r;
    p.c = (start_pos + (start_v * T + 0.5f * accelerate * sq(T)) * axis_r) * A;

    p.left_time = left_time;
}

void ShaperWindow::updateABC() {
    float a = 0, b = 0, c = 0;
    for (int i = 0; i < n; i++) {
        a += params[i].a;
        b += params[i].b;
        c += params[i].c;
    }
    if (ABS(a) < EPSILON) {
        a = 0;
    }
    if (ABS(b) < EPSILON) {
        b = 0;
    }
    if (ABS(c) < EPSILON) {
        c = 0;
    }

    func_params.update(a, b, c);
}

void ShaperWindow::updateParamLeftTime(time_double_t left_time) {
    for (int i = 0; i < n; ++i) {
        if (params[i].left_time == left_time) {
            continue;
        }

        ShaperWindowParams& w_p = params[i];

        float delta_left_time = left_time - params[i].left_time;
        params[i].left_time = left_time;

        w_p.c = w_p.c + w_p.a * sq(delta_left_time) + w_p.b * delta_left_time;
        w_p.b = w_p.b + 2 * w_p.a * delta_left_time;
        w_p.a = w_p.a;
    }
}

void AxisInputShaper::shiftPulses() {
    for (int i = 0; i < params.n; i++) {
        params.T[i] = params.T[i] * 1000;
    }

    float sum_a = 0.;
    for (int i = 0; i < params.n; ++i)
        sum_a += params.A[i];
    float inv_a = 1. / sum_a;
    for (int i = 0; i < params.n; ++i) {
        shift_params.A[params.n - i - 1] = params.A[i] * inv_a;
        shift_params.T[params.n - i - 1] = -params.T[i];
    }
    shift_params.n = params.n;

    float ts = 0.;
    for (int i = 0; i < shift_params.n; ++i)
        ts += shift_params.A[i] * shift_params.T[i];
    for (int i = 0; i < shift_params.n; ++i)
        shift_params.T[i] -= ts;

    left_delta = ABS(shift_params.T[0]);
    right_delta = ABS(shift_params.T[shift_params.n - 1]);
    delta_window = right_delta + left_delta;
}

void AxisInputShaper::init()
{
    params.n = 0;
    switch (type) {
        case InputShaperType::none: {
            break;
        }

        case InputShaperType::zv: {
            const float df = SQRT(1. - sq(zeta));
            const float K = expf(-zeta * M_PI / df);
            const float t_d = 1. / (frequency * df);
            params.n = 2;
            params.A[0] = 1;
            params.A[1] = K;
            params.T[0] = 0;
            params.T[1] = 0.5 * t_d;

            is_shaper_window_init = false;
            break;
        }

        case InputShaperType::zvd: {
            float df = SQRT(1. - sq(zeta));
            float K = expf(-zeta * M_PI / df);
            float t_d = 1. / (frequency * df);
            params.n = 3;
            params.A[0] = 1;
            params.A[1] = 2 * K;
            params.A[2] = sq(K);
            params.T[0] = 0;
            params.T[1] = 0.5 * t_d;
            params.T[2] = t_d;

            is_shaper_window_init = false;
            break;
        }

        case InputShaperType::ei3: {
            float v_tol = 1. / SHAPER_VIBRATION_REDUCTION;
            float df = SQRT(1. - sq(zeta));
            float K = expf(-zeta * M_PI / df);
            float t_d = 1. / (frequency * df);

            float K2 = K * K;
            float a1 = 0.0625 * (1. + 3. * v_tol + 2. * SQRT(2. * (v_tol + 1.) * v_tol));
            float a2 = 0.25 * (1. - v_tol) * K;
            float a3 = (0.5 * (1. + v_tol) - 2. * a1) * K2;
            float a4 = a2 * K2;
            float a5 = a1 * K2 * K2;

            params.n = 5;
            params.A[0] = a1;
            params.A[1] = a2;
            params.A[2] = a3;
            params.A[3] = a4;
            params.A[4] = a5;

            params.T[0] = 0;
            params.T[1] = 0.5 * t_d;
            params.T[2] = t_d;
            params.T[3] = 1.5 * t_d;
            params.T[4] = 2 * t_d;

            is_shaper_window_init = false;
            break;
        }

        default: {
            break;
        }
    }

    shiftPulses();

    LOG_I("n: %d\n", shift_params.n);
    for (int i = 0; i < shift_params.n; i++)
    {
        LOG_I("i: %d, A: %lf, T: %lf\n", i, shift_params.A[i], shift_params.T[i]);
    }
}

float AxisInputShaper::calcPosition(int move_index, time_double_t time, int move_shaped_start, int move_shaped_end) {
    if (moveQueue.getMoveSize() == 0) {
        return 0;
    }

    if (!moveQueue.isBetween(move_index)) {
        return 0;
    }

    float res = 0;
    for (int i = 0; i < shift_params.n; i++) {
        res += shift_params.A[i] * moveQueue.getAxisPositionAcrossMoves(move_index, axis, time + shift_params.T[i], move_shaped_start, move_shaped_end);
    }
    return res;
}


void AxisInputShaper::moveShaperWindowByIndex(int move_shaped_start, int move_shaped_end, time_double_t left_time) {
    int n = shift_params.n;
    shaper_window.n = n;
    shaper_window.zero_n = n - 1;

    Move *move = &moveQueue.moves[move_shaped_start];

    shaper_window.time = move->end_t - right_delta;

    for (int i = n - 1; i >= 0; i--) {
        ShaperWindowParams &w_p = shaper_window.params[i];
        w_p.A = shift_params.A[i];
        w_p.T = shift_params.T[i];

        w_p.time = shaper_window.time + w_p.T;

        while (w_p.time <= move->start_t && move_shaped_start != moveQueue.move_tail) {
            move_shaped_start = moveQueue.prevMoveIndex(move_shaped_start);
            move = &moveQueue.moves[move_shaped_start];
        }

        w_p.move_index = move_shaped_start;
        shaper_window.updateParamABC(i, move->start_v, move->accelerate, move->start_t, left_time, move->start_pos[axis], move->axis_r[axis]);
    }

    shaper_window.updateABC();

    // LOG_I("move_index: %d, %d\n", shaper_window.params[0].move_index, shaper_window.params[1].move_index);

    shaper_window.pos = calcPosition(move_shaped_start, shaper_window.time, move_shaped_start, move_shaped_end);
}


bool AxisInputShaper::moveShaperWindowToNext(uint8_t move_shaped_start, uint8_t move_shaped_end, time_double_t left_time) {
    if (shaper_window.params[shaper_window.n - 1].time == moveQueue.moves[move_shaped_end].end_t)
    {
        return false;
    }

    ShaperWindowParams *zero_p = &shaper_window.params[shaper_window.zero_n];
    zero_p->move_index = moveQueue.nextMoveIndex(zero_p->move_index);

    Move *move = &moveQueue.moves[zero_p->move_index];

    shaper_window.updateParamABC(shaper_window.zero_n, move->start_v, move->accelerate, move->start_t, left_time, move->start_pos[axis], move->axis_r[axis]);

    time_double_t min_next_time = 1000000000.0f;

    for (int i = 0; i < shaper_window.n; i++) {
        ShaperWindowParams &p = shaper_window.params[i];

        time_double_t min_p_next_time = moveQueue.moves[p.move_index].end_t - p.time;

        if (min_p_next_time < min_next_time) {
            min_next_time = min_p_next_time;
            shaper_window.zero_n = i;
        }
    }

    // Cumulative error of processing value
    zero_p = &shaper_window.params[shaper_window.zero_n];
    zero_p->time = moveQueue.moves[zero_p->move_index].end_t;

    for (int i = 0; i < shaper_window.n; i++) {
        if (i == shaper_window.zero_n) {
            continue;
        }
        ShaperWindowParams &p = shaper_window.params[i];
        p.time += min_next_time;
    }

    shaper_window.updateParamLeftTime(left_time);

    shaper_window.updateABC();

    shaper_window.time += min_next_time;
    shaper_window.pos = calcPosition(shaper_window.params[0].move_index, shaper_window.time, shaper_window.params[0].move_index, move_shaped_end);

    return true;
}

bool AxisInputShaper::generateFuncParams(FuncManager& func_manager, uint8_t move_shaper_start, uint8_t move_shaper_end) {
    if (!is_shaper_window_init) {
        moveShaperWindowByIndex(move_shaper_start, move_shaper_end, func_manager.last_time);
        func_manager.addDeltaTimeFuncParams(shaper_window.func_params.a, shaper_window.func_params.b, shaper_window.func_params.c, func_manager.last_time, shaper_window.time, shaper_window.pos);

        is_shaper_window_init = true;
    }

    while (moveShaperWindowToNext(move_shaper_start, move_shaper_end, func_manager.last_time)) {
        func_manager.addDeltaTimeFuncParams(shaper_window.func_params.a, shaper_window.func_params.b, shaper_window.func_params.c, func_manager.last_time, shaper_window.time, shaper_window.pos);
        // printf("axis: %d, generateFuncParams: %lf, %lf, %lf \n", axis, shaper_window.pos, func_manager.getPos(shaper_window.time), shaper_window.time);
    }

    if (func_manager.max_size < func_manager.getSize())
    {
        func_manager.max_size = func_manager.getSize();
    }
    

    return true;
}