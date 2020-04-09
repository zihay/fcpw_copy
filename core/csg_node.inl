namespace fcpw {

template <int DIM>
inline CsgNode<DIM>::CsgNode(const std::shared_ptr<Primitive<DIM>>& left_,
							 const std::shared_ptr<Primitive<DIM>>& right_,
							 const BooleanOperation& operation_):
left(left_),
right(right_),
operation(operation_),
box(false)
{
	LOG_IF(FATAL, left == nullptr || right == nullptr) << "Children cannot be null";
	LOG(INFO) << "Boolean Operation: " << (operation == BooleanOperation::Union ? "Union" :
										  (operation == BooleanOperation::Intersection ? "Intersection" :
										  (operation == BooleanOperation::Difference ? "Difference" :
										   "None")));
	computeBoundingBox();
}

template <int DIM>
inline void CsgNode<DIM>::computeBoundingBox()
{
	if (operation == BooleanOperation::Intersection) {
		// use the child bounding box with the smaller extent; this is not the tightest fit box
		BoundingBox<DIM> leftBox = left->boundingBox();
		BoundingBox<DIM> rightBox = right->boundingBox();
		box.expandToInclude(leftBox.extent().squaredNorm() <
							rightBox.extent().squaredNorm() ?
							leftBox : rightBox);

	} else if (operation == BooleanOperation::Difference) {
		// use the bounding box of the left child (i.e., the object that is subtracted from);
		// this is not the tightest fit box
		box.expandToInclude(left->boundingBox());

	} else {
		// this is the tightest fit box for the union and none operations
		BoundingBox<DIM> leftBox = left->boundingBox();
		BoundingBox<DIM> rightBox = right->boundingBox();
		box.expandToInclude(leftBox);
		box.expandToInclude(rightBox);
		box.isTight = leftBox.isTight && rightBox.isTight;
	}
}

template <int DIM>
inline BoundingBox<DIM> CsgNode<DIM>::boundingBox() const
{
	return box;
}

template <int DIM>
inline Vector<DIM> CsgNode<DIM>::centroid() const
{
	return box.centroid();
}

template <int DIM>
inline float CsgNode<DIM>::surfaceArea() const
{
	// NOTE: this is an overestimate
	return left->surfaceArea() + right->surfaceArea();
}

template <int DIM>
inline float CsgNode<DIM>::signedVolume() const
{
	// NOTE: these are overestimates
	float boxVolume = box.volume();
	if (boxVolume == 0.0f) boxVolume = maxFloat;

	if (operation == BooleanOperation::Intersection) {
		return std::min(boxVolume, std::min(left->signedVolume(), right->signedVolume()));

	} else if (operation == BooleanOperation::Difference) {
		return std::min(boxVolume, left->signedVolume());
	}

	return std::min(boxVolume, left->signedVolume() + right->signedVolume());
}

template <int DIM>
inline void CsgNode<DIM>::computeInteractions(const std::vector<Interaction<DIM>>& isLeft,
											  const std::vector<Interaction<DIM>>& isRight,
											  std::vector<Interaction<DIM>>& is) const
{
	int nLeft = 0;
	int nRight = 0;
	int hitsLeft = (int)isLeft.size();
	int hitsRight = (int)isRight.size();
	bool isLeftIntervalStart = hitsLeft%2 == 0;
	bool isRightIntervalStart = hitsRight%2 == (operation == BooleanOperation::Difference ? 1 : 0);
	int counter = 0;
	if (!isLeftIntervalStart) counter++;
	if (!isRightIntervalStart) counter++;

	auto addInteraction = [](const BooleanOperation& operation, int before, int after) -> bool {
		if (operation == BooleanOperation::Intersection || operation == BooleanOperation::Difference) {
			return (before == 1 && after == 2) || (before == 2 && after == 1);
		}

		// operation is union
		return (before == 0 && after == 1) || (before == 1 && after == 0);
	};

	// traverse the left & right interaction lists, appending interactions based on the operation
	while (nLeft != hitsLeft || nRight != hitsRight) {
		if (operation == BooleanOperation::Intersection && (nLeft == hitsLeft || nRight == hitsRight)) break;
		if (operation == BooleanOperation::Difference && nLeft == hitsLeft) break;

		int counterBefore = counter;
		if (nRight == hitsRight || (nLeft != hitsLeft && isLeft[nLeft].d < isRight[nRight].d)) {
			// left interaction is closer than right interaction
			counter += isLeftIntervalStart ? 1 : -1;
			isLeftIntervalStart = !isLeftIntervalStart;
			if (addInteraction(operation, counterBefore, counter)) is.emplace_back(isLeft[nLeft]);
			nLeft++;

		} else {
			// right interaction is closer than left interaction
			counter += isRightIntervalStart ? 1 : -1;
			isRightIntervalStart = !isRightIntervalStart;
			if (addInteraction(operation, counterBefore, counter)) {
				is.emplace_back(isRight[nRight]);
				if (operation == BooleanOperation::Difference) is[is.size() - 1].n *= -1; // flip normal if operation is difference
			}
			nRight++;
		}
	}
}

template <int DIM>
inline int CsgNode<DIM>::intersect(Ray<DIM>& r, std::vector<Interaction<DIM>>& is,
								   bool checkOcclusion, bool countHits) const
{
	// TODO: optimize for checkOcclusion == true
	int hits = 0;
	is.clear();
	float tMin, tMax;

	if (box.intersect(r, tMin, tMax)) {
		// perform intersection query for left child
		Ray<DIM> rLeft = r;
		std::vector<Interaction<DIM>> isLeft;
		int hitsLeft = left->intersect(rLeft, isLeft, false, true);

		// return if no intersections for the left child were found and
		// the operation is intersection or difference
		if (hitsLeft == 0 && (operation == BooleanOperation::Intersection ||
							  operation == BooleanOperation::Difference)) return 0;

		// perform intersection query for right child
		Ray<DIM> rRight = r;
		std::vector<Interaction<DIM>> isRight;
		int hitsRight = right->intersect(rRight, isRight, false, true);

		// return if no intersections were found for both children
		if (hitsLeft == 0 && hitsRight == 0) return 0;

		if (hitsLeft > 0 && hitsRight > 0) {
			// determine interactions based on the operation
			if (operation == BooleanOperation::None) {
				// merge the left and right sorted interaction lists
				is.resize(isLeft.size() + isRight.size());
				std::merge(isLeft.begin(), isLeft.end(),
						   isRight.begin(), isRight.end(),
						   is.begin(), compareInteractions<DIM>);

			} else {
				computeInteractions(isLeft, isRight, is);
			}

		} else if (hitsLeft > 0) {
			// return if no intersections for the right child were found and the operation
			// is intersection
			if (operation == BooleanOperation::Intersection) return 0;

			// set the interactions to the left child's interactions for the
			// difference, union and none operations
			is = isLeft;

		} else if (hitsRight > 0) {
			// set the interactions to the right child's interactions for the
			// union and none operations
			is = isRight;
		}

		// shrink ray's tMax if possible
		hits = (int)is.size();
		if (!countHits) r.tMax = is[0].d; // list is already sorted
	}

	return hits;
}

template <int DIM>
inline bool CsgNode<DIM>::findClosestPoint(BoundingSphere<DIM>& s, Interaction<DIM>& i) const
{
	bool notFound = true;
	float d2Min, d2Max;

	if (box.overlaps(s, d2Min, d2Max)) {
		// perform closest point query on left child
		Interaction<DIM> iLeft;
		BoundingSphere<DIM> sLeft = s;
		bool foundLeft = left->findClosestPoint(sLeft, iLeft);

		// return if no closest point for the left child is found and
		// the operation is intersection or difference
		if (!foundLeft && (operation == BooleanOperation::Intersection ||
						   operation == BooleanOperation::Difference)) return false;

		// perform closest point query on right child
		Interaction<DIM> iRight;
		BoundingSphere<DIM> sRight = s;
		bool foundRight = right->findClosestPoint(sRight, iRight);

		// return if no closest point was found to both children
		if (!foundLeft && !foundRight) return false;

		if (foundLeft && foundRight) {
			// compute signed distances
			float sdLeft = iLeft.signedDistance(s.c);
			float sdRight = iRight.signedDistance(s.c);
			DistanceInfo info = iLeft.distanceInfo == DistanceInfo::Exact &&
								iRight.distanceInfo == DistanceInfo::Exact ?
								DistanceInfo::Exact : DistanceInfo::Bounded;

			// determine which interaction to set and whether the distance info is
			// exact or bounded based on the operation
			if (operation == BooleanOperation::Union) {
				i = sdLeft < sdRight ? iLeft : iRight; // min(sdLeft, sdRight)
				i.distanceInfo = info == DistanceInfo::Exact &&
								 sdLeft > 0 && sdRight > 0 ?
								 DistanceInfo::Exact : DistanceInfo::Bounded;

			} else if (operation == BooleanOperation::Intersection) {
				i = sdLeft > sdRight ? iLeft : iRight; // max(sdLeft, sdRight)
				i.distanceInfo = info == DistanceInfo::Exact &&
								 sdLeft < 0 && sdRight < 0 ?
								 DistanceInfo::Exact : DistanceInfo::Bounded;

			} else if (operation == BooleanOperation::Difference) {
				iRight.n *= -1; // flip normal of right child
				iRight.sign *= -1; // flip sign of right child
				i = sdLeft > -sdRight ? iLeft : iRight; // max(sdLeft, -sdRight)
				i.distanceInfo = info == DistanceInfo::Exact &&
								 sdLeft < 0 && sdRight > 0 ?
								 DistanceInfo::Exact : DistanceInfo::Bounded;

			} else {
				// set the closer of the two interactions
				i = iLeft.d < iRight.d ? iLeft : iRight;
			}

		} else if (foundLeft) {
			// return if no closest point was found to the right child and the operation
			// is intersection
			if (operation == BooleanOperation::Intersection) return false;

			// set the interaction to the left child's interaction for the
			// difference, union and none operations
			i = iLeft;

		} else if (foundRight) {
			// set the interaction to the right child's interaction for the
			// union and none operations
			i = iRight;
		}

		// shrink sphere radius if possible
		s.r2 = std::min(s.r2, i.d*i.d);
		notFound = false;
	}

	return !notFound;
}

} // namespace fcpw