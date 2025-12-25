#pragma once
/*---------------------------------------------------------------

	procademy MemoryPool.

	메모리 풀 클래스 (오브젝트 풀 / 프리리스트)
	특정 데이타(구조체,클래스,변수)를 일정량 할당 후 나눠쓴다.

	- 사용법.

	procademy::CMemoryPool<DATA> MemPool(300, FALSE);
	DATA *pData = MemPool.Alloc();

	pData 사용

	MemPool.Free(pData);


----------------------------------------------------------------*/
#ifndef  __PROCADEMY_MEMORY_POOL__
#define  __PROCADEMY_MEMORY_POOL__
#include <new.h>
#include <iostream>
#include <vector>
#include <new>
#include <cassert>
#include <utility>

namespace procademy
{

	template <class DATA>
	class CMemoryPool
	{

	private:
		struct st_BLOCK_NODE
		{
			st_BLOCK_NODE* pNext;
		};
		// 스택 방식으로 반환된 (미사용) 오브젝트 블럭을 관리.
		st_BLOCK_NODE* _pFreeNode;
		int m_iCapacity;		// 메모리 풀의 전체 블럭 개수.
		int m_iUseCount;		// 사용중인 블럭 개수.
		bool m_bPlacementNew;	// Alloc 시 생성자 / Free 시 파괴자 호출 여부.
		int m_iTotalCount; // 전체 할당된 블럭 개수

		std::vector<void*> m_vcBuffers;
	public:

		//////////////////////////////////////////////////////////////////////////
		// 생성자, 파괴자.
		//
		// Parameters:	(int) 초기 블럭 개수.
		//				(bool) Alloc 시 생성자 / Free 시 파괴자 호출 여부
		// Return:
		//////////////////////////////////////////////////////////////////////////
		CMemoryPool(int iBlockNum, bool bPlacementNew = false)
			: m_iCapacity(iBlockNum)
			, m_bPlacementNew(bPlacementNew)
			, _pFreeNode(nullptr)
			, m_iUseCount(0)
			, m_iTotalCount(0)
		{
			_allocate(m_iCapacity);
		}
		virtual	~CMemoryPool() {
			for (void* pBuffer : m_vcBuffers)
			{
				::operator delete(pBuffer);
			}
			m_vcBuffers.clear();
		}


		//////////////////////////////////////////////////////////////////////////
		// 블럭 하나를 할당받는다.  
		//
		// Parameters: 없음.
		// Return: (DATA *) 데이타 블럭 포인터.
		//////////////////////////////////////////////////////////////////////////
		template<typename ...Args>
		DATA* Alloc(Args&&... args) {
			if (_pFreeNode == nullptr) {
				_allocate(m_iCapacity);
			}
			// 현재 프리 리스트의 첫 노드를 할당
			st_BLOCK_NODE* pAllocatedNode = _pFreeNode;
			// 프리 리스트의 헤드를 다음 노드로 이동
			_pFreeNode = _pFreeNode->pNext;

			DATA* pData = reinterpret_cast<DATA*>(pAllocatedNode);
			if (m_bPlacementNew) {
				// 생성자 호출
				new (pData) DATA(std::forward<Args>(args)...);
			}
			// 사용량 증가
			m_iUseCount++;
			return pData;
		}

		//////////////////////////////////////////////////////////////////////////
		// 사용중이던 블럭을 해제한다.
		//
		// Parameters: (DATA *) 블럭 포인터.
		// Return: (BOOL) TRUE, FALSE.
		//////////////////////////////////////////////////////////////////////////
		bool	Free(DATA* pData) {
			if (pData == nullptr) return false;

			if (m_bPlacementNew) {
				// 소멸자 호출
				pData->~DATA();
			}
			// 해제된 노드를 프리 리스트의 헤드로 추가
			st_BLOCK_NODE* pNode = reinterpret_cast<st_BLOCK_NODE*>(pData);
			pNode->pNext = _pFreeNode;
			_pFreeNode = pNode;

			// 사용량 감소
			m_iUseCount--;

			return true;
		}


		//////////////////////////////////////////////////////////////////////////
		// 현재 확보 된 블럭 개수를 얻는다. (메모리풀 내부의 전체 개수)
		//
		// Parameters: 없음.
		// Return: (int) 메모리 풀 내부 전체 개수
		//////////////////////////////////////////////////////////////////////////
		int		GetCapacityCount(void) { return m_iCapacity; }

		//////////////////////////////////////////////////////////////////////////
		// 현재 사용중인 블럭 개수를 얻는다.
		//
		// Parameters: 없음.
		// Return: (int) 사용중인 블럭 개수.
		//////////////////////////////////////////////////////////////////////////
		int		GetUseCount(void) { return m_iUseCount; }

	private:
		void _allocate(size_t size) {
			size_t realSize = sizeof(DATA);

			if (realSize < sizeof(st_BLOCK_NODE)) {
				realSize = sizeof(st_BLOCK_NODE);
			}

			char* pBuffer = static_cast<char*>(::operator new(realSize * size));

			// 소멸자 호출 시 메모리 반환용
			m_vcBuffers.push_back(pBuffer);

			char* pCursor = pBuffer;

			for (size_t i = 0; i < size; i++) {
				// 현재 위치를 노드 포인터로 변환
				st_BLOCK_NODE* pCurrentNode = reinterpret_cast<st_BLOCK_NODE*>(pCursor);

				// 다음 위치 계산
				char* pNextCursor = pCursor + realSize;

				// 다음 노드 설정
				if (i < size - 1) {
					pCurrentNode->pNext = reinterpret_cast<st_BLOCK_NODE*>(pNextCursor);
				}
				else {
					pCurrentNode->pNext = _pFreeNode;
				}

				pCursor = pNextCursor;
			}
			_pFreeNode = reinterpret_cast<st_BLOCK_NODE*>(pBuffer);

			m_iTotalCount += static_cast<int>(size);
		}
	};

}

#endif