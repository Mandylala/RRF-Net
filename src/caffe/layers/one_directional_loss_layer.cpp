#include <vector>

#include <algorithm>
#include <cmath>
#include <cfloat>

#include "caffe/util/math_functions.hpp"
#include "caffe/layers/one_directional_loss_layer.hpp"

using std::max;

using namespace std;
using namespace cv;

namespace caffe {

int myrandom (int i) { return caffe_rng_rand()%i;}


template <typename Dtype>
void OneDirectionalLossLayer<Dtype>::Reshape(
  const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top) {
  LossLayer<Dtype>::Reshape(bottom, top);

  img_diff_.ReshapeLike(*bottom[0]);
  text_diff_.ReshapeLike(*bottom[2]);
  dis_.Reshape(bottom[0]->num(), bottom[2]->num(), 1, 1);
  mask_.Reshape(bottom[0]->num(), bottom[2]->num(), 1, 1);
}


template <typename Dtype>
void OneDirectionalLossLayer<Dtype>::set_mask(const vector<Blob<Dtype>*>& bottom)
{

	RankParameter rank_param = this->layer_param_.rank_param();
	int neg_num = rank_param.neg_num();
	float pair_size = rank_param.pair_size();
	float hard_ratio = rank_param.hard_ratio();
	float rand_ratio = rank_param.rand_ratio();
	float margin = rank_param.margin();

	int hard_num = neg_num * hard_ratio;
	int rand_num = neg_num * rand_ratio;

	const Dtype* img_data = bottom[0]->cpu_data();
	const Dtype* img_label = bottom[1]->cpu_data();
        const Dtype* text_data = bottom[2]->cpu_data();
        const Dtype* text_label = bottom[3]->cpu_data();

	int img_count = bottom[0]->count();
	int img_num = bottom[0]->num();
	int img_dim = bottom[0]->count() / bottom[0]->num();

	int text_count = bottom[2]->count();
	int text_num = bottom[2]->num();
	int text_dim = bottom[2]->count() / bottom[2]->num();

	Dtype* dis_data = dis_.mutable_cpu_data();
	Dtype* mask_data = mask_.mutable_cpu_data();

	for(int i = 0; i < img_num * text_num; i ++)
	{
		dis_data[i] = 0;
		mask_data[i] = 0;
	}

	// calculate distance
	for(int i = 0; i < img_num; i ++)
	{
		for(int j = 0; j < text_num; j ++)
		{
			const Dtype* fea1 = img_data + i * img_dim;
			const Dtype* fea2 = text_data + j * text_dim;
			Dtype ts = 0;
			for(int k = 0; k < img_dim; k ++)
			{
			  ts += (fea1[k] * fea2[k]) ;
			}
			dis_data[i * text_num + j] = -ts;
		}
	}

	//select samples

	vector<pair<float, int> >negpairs;
	vector<int> sid1;
	vector<int> sid2;


	for(int i = 0; i < img_num; i ++)
	{
		negpairs.clear();
		sid1.clear();
		sid2.clear();
		for(int j = 0; j < text_num; j ++)
		{
			if(img_label[i] == text_label[j])
			    continue;
			Dtype tloss = max(Dtype(0), dis_data[i * text_num + i] - dis_data[i * text_num + j] + Dtype(margin));
			if(tloss == 0) continue;

			negpairs.push_back(make_pair(dis_data[i * text_num + j], j));
		}
                // if valid negpairs in batch size are less than the expected neg_num 
		if(negpairs.size() <= neg_num)
		{
			for(int j = 0; j < negpairs.size(); j ++)
			{
				int id = negpairs[j].second;
				mask_data[i * text_num + id] = 1;
			}
			continue;
		}
                
                // else valid negpairs in batch size are more than the expected neg_num
		sort(negpairs.begin(), negpairs.end());

		for(int j = 0; j < neg_num; j ++)
		{
			sid1.push_back(negpairs[j].second);
		}
		for(int j = neg_num; j < negpairs.size(); j ++)
		{
			sid2.push_back(negpairs[j].second);
		}
		std::random_shuffle(sid1.begin(), sid1.end(), myrandom);
		for(int j = 0; j < min(hard_num, (int)(sid1.size()) ); j ++)
		{
			mask_data[i * text_num + sid1[j]] = 1;
		}
		for(int j = hard_num; j < sid1.size(); j ++)
		{
			sid2.push_back(sid1[j]);
		}
		std::random_shuffle(sid2.begin(), sid2.end(), myrandom);
		for(int j = 0; j < min( rand_num, (int)(sid2.size()) ); j ++)
		{
			mask_data[i * text_num + sid2[j]] = 1;
		}

	}

}

template <typename Dtype>
void OneDirectionalLossLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {

	//const Dtype* img_data = bottom[0]->cpu_data();
	//const Dtype* img_label = bottom[1]->cpu_data();
        //const Dtype* text_data = bottom[2]->cpu_data();
        //const Dtype* text_label = bottom[3]->cpu_data();

	//int img_count = bottom[0]->count();
	int img_num = bottom[0]->num();
	//int img_dim = bottom[0]->count() / bottom[0]->num();

	//int text_count = bottom[2]->count();
	int text_num = bottom[2]->num();
	//int text_dim = bottom[2]->count() / bottom[2]->num();


	RankParameter rank_param = this->layer_param_.rank_param();
	int neg_num = rank_param.neg_num();      // 4
	float pair_size = rank_param.pair_size();  // 5
	//float hard_ratio = rank_param.hard_ratio();
	//float rand_ratio = rank_param.rand_ratio();
	float margin = rank_param.margin();
	Dtype* dis_data = dis_.mutable_cpu_data();
	Dtype* mask_data = mask_.mutable_cpu_data();

	set_mask(bottom);
	Dtype loss = 0;
	//int cnt = neg_num * img_num / pair_size * 2;
        int cnt = neg_num * img_num;

	for(int i = 0; i < img_num; i ++)
	{
		for(int j = 0; j < text_num; j ++)
		{
			if(mask_data[i * text_num + j] == 0) continue;
			Dtype tloss1 = pair_size * max(Dtype(0), dis_data[i * text_num + i] - dis_data[i * text_num + j] + Dtype(margin));
			//Dtype tloss2 = max(Dtype(0), dis_data[i * text_num + i] - dis_data[(i + 1) * num + j] + Dtype(margin));
			//loss += tloss1 + tloss2;
                        loss += tloss1;
		}
	}

	loss = loss / cnt;
	top[0]->mutable_cpu_data()[0] = loss;
}

template <typename Dtype>
void OneDirectionalLossLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {

	const Dtype* img_data = bottom[0]->cpu_data();
	//const Dtype* img_label = bottom[1]->cpu_data();
        const Dtype* text_data = bottom[2]->cpu_data();
        //const Dtype* text_label = bottom[3]->cpu_data();

	int img_count = bottom[0]->count();
	int img_num = bottom[0]->num();
	int img_dim = bottom[0]->count() / bottom[0]->num();

	int text_count = bottom[2]->count();
	int text_num = bottom[2]->num();
	int text_dim = bottom[2]->count() / bottom[2]->num();

        Dtype* img_diff = bottom[0]->mutable_cpu_diff();
        Dtype* text_diff = bottom[2]->mutable_cpu_diff();

	RankParameter rank_param = this->layer_param_.rank_param();
	int neg_num = rank_param.neg_num();
	float pair_size = rank_param.pair_size();
	//float hard_ratio = rank_param.hard_ratio();
	//float rand_ratio = rank_param.rand_ratio();
	float margin = rank_param.margin();

	Dtype* dis_data = dis_.mutable_cpu_data();
	Dtype* mask_data = mask_.mutable_cpu_data();

	for(int i = 0; i < img_count; i ++ )
        {
		img_diff[i] = 0;
                text_diff[i] = 0;
        }

	//int cnt = neg_num * num / pair_size * 2;
        int cnt = neg_num * img_num;

	for(int i = 0; i < img_num; i ++)
	{
                const Dtype* fori = img_data + i * img_dim;
	        const Dtype* fpos = text_data + i * text_dim;

	        Dtype* fori_diff = img_diff + i * img_dim;
		Dtype* fpos_diff = text_diff + i * text_dim;
		for(int j = 0; j < text_num; j ++)
		{
			if(mask_data[i * text_num + j] == 0) continue;
			Dtype tloss1 = pair_size * max(Dtype(0), dis_data[i * text_num + i] - dis_data[i * text_num + j] + Dtype(margin));
			//Dtype tloss2 = max(Dtype(0), dis_data[i * num + i + 1] - dis_data[(i + 1) * num + j] + Dtype(margin));

			const Dtype* fneg = text_data + j * text_dim;
			Dtype* fneg_diff = text_diff + j * text_dim;
			if(tloss1 > 0)
			{
			    for(int k = 0; k < img_dim; k ++)
			    {
					fori_diff[k] += pair_size * (fneg[k] - fpos[k]); // / (pairNum * 1.0 - 2.0);
					fpos_diff[k] += -pair_size * fori[k]; // / (pairNum * 1.0 - 2.0);
					fneg_diff[k] +=  pair_size * fori[k];
			    }
			}
                        /*
			if(tloss2 > 0)
			{
				for(int k = 0; k < dim; k ++)
				{
				    fori_diff[k] += -fpos[k]; // / (pairNum * 1.0 - 2.0);
				    fpos_diff[k] += fneg[k]-fori[k]; // / (pairNum * 1.0 - 2.0);
				    fneg_diff[k] += fpos[k];
				}
			}
                        */

		}
	}

	for (int i = 0; i < img_count; i ++)
	{
		img_diff[i] = img_diff[i] / cnt;
                text_diff[i] = text_diff[i] / cnt;
	}

}

#ifdef CPU_ONLY
STUB_GPU(OneDirectionalLossLayer);
#endif

INSTANTIATE_CLASS(OneDirectionalLossLayer);
REGISTER_LAYER_CLASS(OneDirectionalLoss);

}  // namespace caffe
