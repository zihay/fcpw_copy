#pragma once

#include "bvh.h"

namespace fcpw {

template <int DIM>
class Sbvh: public Aggregate<DIM> {
public:
	// constructor
	Sbvh(std::vector<std::shared_ptr<Primitive<DIM>>>& primitives_,
		 const CostHeuristic& costHeuristic_, int leafSize_=4);

	// returns bounding box
	BoundingBox<DIM> boundingBox() const;

	// returns centroid
	Vector<DIM> centroid() const;

	// returns surface area
	float surfaceArea() const;

	// returns signed volume
	float signedVolume() const;

	// intersects with ray
	int intersect(Ray<DIM>& r, std::vector<Interaction<DIM>>& is,
				  bool checkOcclusion=false, bool countHits=false) const;

	// finds closest point to sphere center
	bool findClosestPoint(BoundingSphere<DIM>& s, Interaction<DIM>& i) const;

protected:
	// builds binary tree
	void build(const CostHeuristic& costHeuristic);

	// members
	int nNodes, nLeafs, leafSize;
	const std::vector<std::shared_ptr<Primitive<DIM>>>& primitives;
};

} // namespace fcpw

#include "sbvh.inl"