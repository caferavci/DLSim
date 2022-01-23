/* Portions Copyright 2019-2021 Xuesong Zhou and Peiheng Li, Cafer Avci
 
 * If you help write or modify the code, please also list your names here.
 * The reason of having Copyright info here is to ensure all the modified version, as a whole, under the GPL
 * and further prevent a violation of the GPL.
 *
 * More about "How to use GNU licenses for your own software"
 * http://www.gnu.org/licenses/gpl-howto.html
 */

// Peiheng, 02/03/21, remove them later after adopting better casting
#pragma warning(disable : 4305 4267 4018)
// stop warning: "conversion from 'int' to 'float', possible loss of data"
#pragma warning(disable: 4244)

#ifdef _WIN32
#include "pch.h"
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <functional>
#include <stack>
#include <list>
#include <vector>
#include <map>
#include <omp.h>
#include "config.h"
#include "utils.h"


using std::max;
using std::min;
using std::cout;
using std::endl;
using std::string;
using std::vector;
using std::map;
using std::ifstream;
using std::ofstream;
//using std::istringstream;

class CPeriod_VDF
{
public:
    CPeriod_VDF() : vdf_type{ 0 }, Q_cd{ 0.954946463 }, Q_n{ 1.124 }, Q_cp{0.6},  Q_s{ 1 }, vf{ 60 }, v_cutoff{ 45 }, FFTT{ 1 }, VCTT{ 1 }, peak_load_factor{ 1 }, queue_demand_factor{ 0 }, DOC{ 0 }, VOC{ 0 }, vt2{ -1 },
        alpha{ 0.52f }, beta{ 1.29 }, Q_alpha{ 0.368 }, Q_beta{ 1.38 }, rho{ 1 }, preload{ 0 }, penalty{ 0 }, sa_lanes_change{ 0 }, LR_price{ 0 }, LR_RT_price{ 0 }, starting_time_in_hour{ 0 }, ending_time_in_hour{ 0 },
        cycle_length{ -1 }, red_time{ 0 }, effective_green_time{ 0 }, t0{ -1 }, t3{ -1 }, start_green_time{ -1 }, end_green_time{ -1 }, L{ 1 },
        queue_length{ 0 }, avg_waiting_time{ 0 }, P{ -1 }, Severe_Congestion_P{ -1 }, lane_based_D{ 0 }, lane_based_Vph{ 0 }, avg_speed_BPR{ -1 }, avg_queue_speed{ -1 }, nlanes{ 1 }, sa_volume{ 0 }, t2{ 1 }, k_critical{ 45 }
{
        for (int at = 0; at < MAX_AGNETTYPES; at++)
        {
            toll[at] = 0;
            pce[at] = 1;
        }

    }

    ~CPeriod_VDF()
    {
    }

    //float PerformSignalVDF(float hourly_per_lane_volume, float red, float cycle_length)
    //{
    //    float lambda = hourly_per_lane_volume;
    //    float mu = _default_saturation_flow_rate; //default saturation flow ratesa
    //    float s_bar = 1.0 / 60.0 * red * red / (2 * cycle_length); // 60.0 is used to convert sec to min
    //    float uniform_delay = s_bar / max(1 - lambda / mu, 0.1f);

    //    return uniform_delay;
    //}


    double get_speed_from_volume(float hourly_volume, float vf, float k_critical, float s3_m)
    {
        //test data free_speed = 55.0f; 
        //speed = 52;
        //k_critical = 23.14167648;

        double max_lane_capacity = k_critical * vf / pow(2, 2 / s3_m);

        if (hourly_volume > max_lane_capacity)
            hourly_volume = max_lane_capacity;
        // we should add a capacity upper bound on hourly_volume;

        double coef_a = pow(k_critical, s3_m);
        double coef_b = pow(k_critical, s3_m) * pow(vf, s3_m / 2.0);
        double coef_c = pow(hourly_volume, s3_m);  // D is hourly demand volume, which is equivalent to flow q in S3 model

        double speed = pow((coef_b + pow(coef_b * coef_b - 4.0 * coef_a * coef_c, 0.5)) / (2.0 * coef_a), 2.0 / s3_m);    //under uncongested condition
        if (speed >= vf)
            speed = vf;

        if (speed < 0)
            speed = 0;

        return speed;

    }

    double get_volume_from_speed(float speed, float vf, float k_critical, float s3_m)
    {
        //test data free_speed = 55.0f; 
        //speed = 52;
        //k_critical = 23.14167648;

        if (speed < 0)
            return -1;

        double speed_ratio = vf / max(1, speed);
        if (speed_ratio <= 1.00001)
            speed_ratio = 1.00001;

        /*   float volume = 0;*/
        double ratio_difference = pow(speed_ratio, s3_m / 2) - 1;

        double ratio_difference_final = max(ratio_difference, 0.00000001);

        double volume = speed * k_critical * pow(ratio_difference_final, 1 / s3_m);  // volume per hour per lane

        return volume;

    }

  

    double calculate_travel_time_based_on_QVDF(double volume, 
        float model_speed[MAX_TIMEINTERVAL_PerDay], float est_volume_per_hour_per_lane[MAX_TIMEINTERVAL_PerDay])    
    {
        // QVDF

            // step 1: calculate lane_based D based on plf and nlanes from link volume V over the analysis period  take nonnegative values
            lane_based_D = max(0.0, volume) * queue_demand_factor / max(1, nlanes);
            // step 2: D_ C ratio based on lane-based D and lane-based ultimate hourly capacity, 
            // uncongested states D <C 
            // congested states D > C, leading to P > 1
            DOC = lane_based_D / max(0.00001, lane_based_ultimate_hourly_capacity);
            
            //step 3.1 fetch vf and v_cutoff based on FFTT, VCTT (to be compartible with transit data, such as waiting time )
            // we could have a period based FFTT, so we need to convert FFTT to vfree
            // if we only have one period, then we can directly use vf and v_cutoff. 

            //step 3.2 calculate speed from VDF based on D/C ratio
            avg_queue_speed = v_cutoff / (1.0 + Q_alpha * pow(DOC, Q_beta));
            // step 3.3 taking the minimum of BPR- v and Q VDF v based on log sum function 


       // BPR
            lane_based_Vph = max(0.0, volume) * peak_load_factor / max(1, nlanes);
            // step 2: D_ C ratio based on lane-based D and lane-based ultimate hourly capacity, 
            // uncongested states D <C 
            // congested states D > C, leading to P > 1
            VOC = lane_based_Vph / max(0.00001, lane_based_ultimate_hourly_capacity);

            //step 3.1 fetch vf and v_cutoff based on FFTT, VCTT (to be compartible with transit data, such as waiting time )
            // we could have a period based FFTT, so we need to convert FFTT to vfree
            // if we only have one period, then we can directly use vf and v_cutoff. 

            //step 3.2 calculate speed from VDF based on D/C ratio
            avg_speed_BPR = vf / (1.0 + alpha * pow(VOC, beta));
            avg_travel_time = FFTT * vf / max(0.1, avg_speed_BPR); // Mark: FFTT should be vctt

            if (vdf_type == 0)  // pure BPR form
                avg_travel_time = FFTT * vf / max(0.1, avg_speed_BPR); // Mark: FFTT should be vctt
            else if (vdf_type == 1) // pure QVDF form
            {
                avg_travel_time = VCTT * v_cutoff / max(0.1, avg_queue_speed); // Mark: FFTT should be vctt

            }else        // BPR-X form
            {
                if (DOC > 0.2)
                {
                    avg_travel_time = VCTT * v_cutoff / max(0.1, avg_queue_speed);
                }
                else
                {
                    avg_travel_time = FFTT * vf / max(0.1, avg_speed_BPR); // Mark: FFTT should be vctt
                }

            }
            

            //step 4.4 compute vt2
//            vt2 = avg_queue_speed * 8.0 / 15.0;  // 8/15 is a strong assumption 
            P = Q_cd * pow(DOC, Q_n);  // applifed for both uncongested and congested conditions
            double base = Q_cp*pow(P, Q_s) + 1.0;
            vt2 = v_cutoff / max(0.001, base);
            //step 4.1: compute congestion duration P
            
            
            double nonpeak_hourly_flow = 0;
               
               if(L - P >= 10.0 / 60.0)
               {
                   nonpeak_hourly_flow = (volume * (1- peak_load_factor)) / max(1, nlanes) / max(0.1,min(L-1, L - P - 5.0/60.0));  //5.0/60.0 as one 5 min interval, as P includes both boundary points
               }

           //           dtalog.output() << "nonpeak_hourly_flow = " << nonpeak_hourly_flow << endl;

           // setup the upper bound on nonpeak flow rates
           if (nonpeak_hourly_flow > lane_based_ultimate_hourly_capacity)
               nonpeak_hourly_flow = lane_based_ultimate_hourly_capacity;

           double nonpeak_avg_speed = (vf + v_cutoff) / 2.0; // later we will use piecewise approximation 

           //step 4.2 t0 and t3
           t0 = t2 - 0.5 * P;
           t3 = t2 + 0.5 * P;

           // work on congested condition
           //step 4.3 compute mu
            double mu = min(lane_based_ultimate_hourly_capacity, lane_based_D / P);

           //use  as the lower speed compared to 8/15 values for the congested states 


           // let us use link_length_in_km = 1 for the following calculation 
           double  link_length_in_1km = 1.0;
           double RTT = link_length_in_1km / max(0.01,avg_queue_speed);  // in hour
           if (P > 1)  // congested
           {
               RTT = link_length_in_1km / v_cutoff;
           }


           double wt2 = link_length_in_1km / vt2 - RTT; // in hour


           //step 5 compute gamma parameter is controlled by the maximum queue
           double gamma = wt2 * 64*mu / pow(P, 4);  // because q_tw = w*mu =1/4 * gamma (P/2)^4, => 1/vt2 * mu = 1/4 * gamma  * (P/2)^4

            //QL(t2) = gamma / (4 * 4 * 4) * power(P, 4)
           double test_QL_t2 = gamma / 64.0 * pow(P, 4);
           double test_wt2 = test_QL_t2 / mu;

           //L/[(w(t)+RTT_in_hour]
           double test_vt2 = link_length_in_1km/(test_wt2 + RTT);

           //ensure 
           //ensure diff_v_t2 = 0;
           double diff = test_vt2 - vt2;
           double td_w = 0;
           //step scan the entire analysis period
           Severe_Congestion_P = 0;
           for (int t_in_min = starting_time_in_hour * 60; t_in_min < ending_time_in_hour * 60; t_in_min += 5)
           {
               double t = t_in_min / 60.0;  // t in hour
               double td_queue = 0;
               double td_speed = 0;

               if (t0 <= t && t <= t3)  // within congestion duration P
               {
                   //1/4*gamma*(t-t0)^2(t-t3)^2
                   td_queue = 0.25 * gamma * pow((t - t0), 2) * pow((t - t3), 2);
                   td_w = td_queue / max(0.001,mu);
                   //L/[(w(t)+RTT_in_hour]
                   td_speed = link_length_in_1km / (td_w + RTT);
               }
               else if (t < t0) //outside
               {
                 td_queue = 0;
                 double factor = (t - starting_time_in_hour) / max(0.001, t0 - starting_time_in_hour);
                 td_speed =  (1 - factor)* vf + factor * max(v_cutoff,avg_queue_speed);
               }
               else  // t> t3
               {
                   td_queue = 0;
                   double factor = (t - t3) / max(0.001, ending_time_in_hour - t3);
                   td_speed = (1 - factor) * max(v_cutoff, avg_queue_speed) + (factor)*vf;
               }

               // cout << "td_queue t" << t << " =  " << td_queue << ", speed =" << td_speed << endl;

            int t_interval = t_in_min / 5;
            double td_flow = 0; // default: get_volume_from_speed(td_speed, vf, k_critical, s3_m);
            model_speed[t_interval] = td_speed;
            est_volume_per_hour_per_lane[t_interval] = td_flow;

            if(td_speed < vf*0.5)
                Severe_Congestion_P += 5.0/16;  // 5 min interval
                
           }

           //// peak load duration
           //double pl_t0 = t2 - max(0.5, 0.5 * P);
           //double pl_t3 = t2 + max(0.5, 0.5 * P);
           //double est_peak_load_demand = 0;
           ////est_non_peak_load_demand should not be counted, as avg non-peak rates have been determined by (V-D)/(L-P)

           //double hourly_rate_2_volume_factor = nlanes / 12.0;  // /12 to convert hourly to 5 min volume;
           //// step 2
           //for (int t_in_min = assign_period_start_time_in_hour * 60; t_in_min < assign_period_end_time_in_hour * 60; t_in_min += 5)
           //{
           //    double t = t_in_min / 60.0;  // t in hour
           //    int t_interval = t_in_min / 5;
           //    
           //     if (t >= pl_t0  && t <= pl_t3)
           //        {
           //         est_peak_load_demand += est_volume_per_hour_per_lane[t_interval] * hourly_rate_2_volume_factor;
           //        }
           //}
           //// step 3:
           //double peak_load_volume_scale_factor = lane_based_D / max(0.0001,est_peak_load_demand);


           ////step 4
           //for (int t_in_min = assign_period_start_time_in_hour * 60; t_in_min < assign_period_end_time_in_hour * 60; t_in_min += 5)
           //{
           //    double t = t_in_min / 60.0;  // t in hour
           //    int t_interval = t_in_min / 5;

           //    if (t < pl_t0)
           //    {
           //        est_volume_per_hour_per_lane[t_interval] = min(lane_based_ultimate_hourly_capacity, est_volume_per_hour_per_lane[t_interval]);
           //    }
           //    else if (t > pl_t3)
           //    {
           //        est_volume_per_hour_per_lane[t_interval] = min(lane_based_ultimate_hourly_capacity, est_volume_per_hour_per_lane[t_interval]);
           //    }
           //    else
           //    {
           //        est_volume_per_hour_per_lane[t_interval] = min(lane_based_ultimate_hourly_capacity, est_volume_per_hour_per_lane[t_interval] * peak_load_volume_scale_factor);
           //    }
           //}


           //final stage: avg delay in peak load hours

            return avg_travel_time;
     }

    int vdf_type;
    double DOC;
    double VOC;
    //updated BPR-X parameters
    double vt2;
    //peak hour factor
    double alpha;
    double beta;

    double Q_alpha;
    double Q_beta;
    double Q_cd;
    double Q_cp;
    double Q_n;
    double Q_s;


    double starting_time_in_hour;
    double ending_time_in_hour;
    double t2;
    double k_critical;

    double peak_load_factor;  // peak load factor
    double queue_demand_factor;  // queue_demand_factor

    double v_cutoff;
    double vf;

    double sa_volume;
    double sa_lanes_change;
    double preload;
    double toll[MAX_AGNETTYPES];
    double pce[MAX_AGNETTYPES];
    double occ[MAX_AGNETTYPES]; // person based occupancy 
    double penalty;
    double LR_price[MAX_AGNETTYPES];
    double LR_RT_price[MAX_AGNETTYPES];;
    string allowed_uses;

    double rho;
//    double marginal_base;
    // in 15 min slot
    float cycle_length;
    float red_time;
    float effective_green_time;
    int start_green_time;
    int end_green_time;

    double t0, t3;

    bool bValidQueueData;
    string period;

//    double period_capacity;  // link based period_capacity  //depreciated; will not be used. 
    double lane_based_ultimate_hourly_capacity;
    double nlanes;

    double FFTT;
    double VCTT;
    double P;
    double Severe_Congestion_P;
    double L;
    double lane_based_D;
    double lane_based_Vph;
    
    double avg_speed_BPR;  // normal BPR
    double avg_queue_speed;  // queue VDF speed
    // inpput
    double volume;

    //output
    double avg_delay;
    double avg_travel_time;
    double total_travel_time;

    //// t starting from starting_time_slot_no if we map back to the 24 hour horizon
    float queue_length;
    float arrival_flow_volume;
    float discharge_rate;  // period based
    float avg_waiting_time;
    float travel_time;

    std::map<int, float> travel_time_per_iteration_map;
};



