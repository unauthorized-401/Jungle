#include "rbtree.h"

#include <stdlib.h>

// Jiwon Functions

void delete_node(node_t *p, rbtree *t);
void left_rotate(rbtree *t, node_t *x);
void right_rotate(rbtree *t, node_t *x);
void rbtree_insert_fixup(rbtree *t, node_t *z);
void rbtree_transplant(rbtree *t, node_t *u, node_t *v);
void rbtree_delete_fixup(rbtree *t, node_t *x);
int inorder_array(node_t *p, const rbtree *t, key_t *arr, const size_t n, int index);

// RB tree 구조체 생성
// 여러 개의 tree를 생성할 수 있어야 하며 각각 다른 내용들을 저장할 수 있어야 합니다.
rbtree *new_rbtree(void) {
  rbtree *p = (rbtree *)calloc(1, sizeof(rbtree));

  // TODO: initialize struct if needed

  node_t *node = (node_t *)calloc(1, sizeof(node_t));
  node->color = RBTREE_BLACK;

  // Black 노드 생성하여 Root, Nil 노드에 할당
  p->root = node;
  p->nil = node;

  return p;
}

// RB tree 구조체가 차지했던 메모리 반환
// 해당 tree가 사용했던 메모리를 전부 반환해야 합니다. (valgrind로 나타나지 않아야 함)
void delete_rbtree(rbtree *t) {
  // TODO: reclaim the tree nodes's memory

  delete_node(t->root, t);

  free(t->nil);
  t->nil = NULL;

  free(t);
  t = NULL;
}

// RB tree에 key 추가
// 구현하는 ADT가 multiset이므로 이미 같은 key의 값이 존재해도 하나 더 추가 합니다.
node_t *rbtree_insert(rbtree *t, const key_t key) {
  // TODO: implement insert

  // Insert할 새 노드 생성
  node_t *z = (node_t *)calloc(1, sizeof(node_t));
  z->key = key;

  // Root, Nil 노드를 가리키는 임시 포인터
  node_t *x = t->root;
  node_t *y = t->nil;

  while (x != t->nil) {
    y = x;

    if (key < x->key) {
      x = x->left;
    } else {
      x = x->right;
    }
  }

  // 새 노드의 부모를 Y로 지정
  z->parent = y;

  // 값을 비교하며 왼쪽 또는 오른쪽 자식으로 내려감
  if (y == t->nil) {
    t->root = z;
  } else if (key < y->key) {
    y->left = z;
  } else {
    y->right = z;
  }

  // 새 노드의 자식들 설정
  z->left = t->nil;         
  z->right = t->nil;
  // 삽입하는 노드는 항상 RED
  z->color = RBTREE_RED;

  rbtree_insert_fixup(t, z);

  return t->root;
}

// RB tree내에 해당 key가 있는지 탐색하여 있으면 해당 node pointer 반환
// 해당하는 node가 없으면 NULL 반환
node_t *rbtree_find(const rbtree *t, const key_t key) {
  // TODO: implement find

  node_t *x = t->root;

  if (x == t->nil) {
    return NULL;
  }

  while (x != t->nil) {
    if (key < x->key) {
      x = x->left;

    } else if (key > x->key) {
      x = x->right;

    } else {
      return x;
    }
  }

  return NULL;             
}

// RB tree 중 최소값을 가진 node pointer 반환
node_t *rbtree_min(const rbtree *t) {
  // TODO: implement find

  // 왼쪽 자식으로 계속 내려가면 됨
  node_t *min = t->root;

  while (min->left != t->nil) {
    min = min->left;
  }

  return min;
}

// RB tree 중 최대값을 가진 node pointer 반환
node_t *rbtree_max(const rbtree *t) {
  // TODO: implement find

  // 오른쪽 자식으로 계속 내려가면 됨
  node_t *max = t->root;             
  while (max->right != t->nil) {
    max = max->right;
  }

  return max;
}

// RB tree 내부의 ptr로 지정된 node를 삭제하고 메모리 반환
int rbtree_erase(rbtree *t, node_t *z) {
  // TODO: implement erase
  
  // 삭제된 노드이거나 이동할 노드
  node_t *y = z;
  color_t y_original_color = y->color;
  
  // 삭제할 노드의 남은 트리 저장
  node_t *x;

  // 1) 오른쪽 자식만 있는 경우
  if (z->left == t->nil) {
    x = z->right;
    // Z와 Z의 오른쪽 자식 교환
    rbtree_transplant(t, z, z->right);
  
  // 2) 왼쪽 자식만 있는 경우
  } else if (z->right == t->nil) {
    x = z->left;
    // Z와 Z의 왼쪽 자식 교환
    rbtree_transplant(t, z, z->left);

  // 3) 두 자식 다 있는 경우
  } else {
    // Z를 대체할 다음값 찾기, 즉 Z의 Successor를 Y에 저장
    y = z->right;
    while (y->left != t->nil) {
      y = y->left;
    }

    y_original_color = y->color;
    x = y->right;
	
    if (y->parent == z) {
      // 삭제할 노드와 해당 노드의 자식 노드 사이에 어떠한 노드도 없을 경우,
      // 자식 노드를 부모 노드 자리로 이동시켜주기만 하면 됨
      x->parent = y;

    } else {
      // 삭제할 노드와 해당 노드의 자식 노드 사이에 다른 노드들이 있을 경우,
      rbtree_transplant(t, y, y->right);
      y->right = z->right;
      y->right->parent = y;
    }

    rbtree_transplant(t, z, y);
    y->left = z->left;
    y->left->parent = y;
    y->color = z->color;
  }

  // 삭제하려는 색이 RED라면, 어떠한 속성도 위반하지 않음
  // 하지만 삭제하려는 색이 BLACK이라면, 속성 위반 가능
  if (y_original_color == RBTREE_BLACK) {
    rbtree_delete_fixup(t, x);
  }
  
  // 트리와 떨어진 노드 반환: delete는 트리와 이어진 노드만 반환하기 때문
  free(z);
  z = NULL;

  return 0;
}

// RB tree의 내용을 key 순서대로 주어진 array로 변환
// array의 크기는 n으로 주어지며 tree의 크기가 n 보다 큰 경우에는 순서대로 n개 까지만 변환
// array의 메모리 공간은 이 함수를 부르는 쪽에서 준비하고 그 크기를 n으로 알려줍니다.
int rbtree_to_array(const rbtree *t, key_t *arr, const size_t n) {
  // TODO: implement to_array

  inorder_array(t->root, t, arr, n, 0);

  return 0;
}

// Jiwon Functions

void delete_node(node_t *p, rbtree *t) {
  if (p == t->nil) {
    return;
  }

  delete_node(p->left, t);
  delete_node(p->right, t);

  free(p);
  p = NULL;
}

void left_rotate(rbtree *t, node_t *x) {
  // X와 Y의 상관관계 저장
  node_t *y = x->right;
  x->right = y->left;

  // Y의 왼쪽 자식이 유효한 값이면,
  if (y->left != t->nil) {
    y->left->parent = x;
  }
  // X의 부모를 Y로 연결
  y->parent = x->parent;

  // X의 부모가 nil이라는 것은, X가 root라는 뜻
  if (x->parent == t->nil) {
    t->root = y;
  } else if (x == x->parent->left) {
    x->parent->left = y;
  } else {
    x->parent->right = y;
  }

  // X와 Y의 상관관계 저장
  y->left = x;
  x->parent = y;
}

void right_rotate(rbtree *t, node_t *x) {
  // X와 Y의 상관관계 저장
  node_t *y = x->left; 
  x->left = y->right;

  // X의 오른쪽 자식이 유효한 값이면, 즉 nil이 아니라면,
  if (y->right != t->nil) {
    y->right->parent = x;
  }
  // Y의 부모를 X로 연결
  y->parent = x->parent; 

  // Y의 부모가 nil이라는 것은, Y가 root라는 뜻
  if (x->parent == t->nil) {
    t->root = y;
  } else if (x == x->parent->right) {
    x->parent->right = y;
  } else {
    x->parent->left = y;
  }

  // X와 Y의 상관관계 저장
  y->right = x;
  x->parent = y;
}

// Case 1: Z의 삼촌 Y가 RED인 경우
// Case 2: Z의 삼촌 Y가 BLACK이며, Z가 오른쪽 자식인 경우
// Case 3: Z의 삼촌 Y가 BLACK이며, Z가 왼쪽 자식인 경우

void rbtree_insert_fixup(rbtree *t, node_t *z) {
  // 부모의 색이 RED인 노드만 해당됨: 4, 5번 속성을 만족시키는 지 확인하기 위해
  while (z->parent->color == RBTREE_RED) {
    // 1) Z의 부모가 왼쪽 자식이라면,
    if (z->parent == z->parent->parent->left) {
      // 오른쪽 자식은 삼촌이겠지
      node_t *y = z->parent->parent->right;

      // Case 1: 할아버지 RED, 삼촌 RED
      if (y->color == RBTREE_RED) {
        // 부모/삼촌 Black으로, 할아버지 Red로
        z->parent->color = RBTREE_BLACK;
        y->color = RBTREE_BLACK;
        z->parent->parent->color = RBTREE_RED;
        // 시점을 할아버지로 변경, 할아버지에서 다시 확인
        z = z->parent->parent;

      } else {
        // Case 2: Z가 부모의 오른쪽 자식이라면,
        if (z == z->parent->right) {
          // 부모를 좌회전해야겠지
          z = z->parent;
          left_rotate(t, z);
        }

        // Case 3: Z는 부모의 왼쪽 자식이기 때문에
        // 색 지정하고 우회전해야겠지
        z->parent->color = RBTREE_BLACK;
        z->parent->parent->color = RBTREE_RED;
        right_rotate(t, z->parent->parent);
      }

    // 2) Z의 부모가 오른쪽 자식이라면,
    } else {
      // 왼쪽 자식은 삼촌이겠지
      node_t *y = z->parent->parent->left;

      // Case 1: 할아버지 RED, 삼촌 RED
      if (y->color == RBTREE_RED) {
        // 부모/삼촌 Black으로, 할아버지 Red로
        z->parent->color = RBTREE_BLACK;
        y->color = RBTREE_BLACK;
        z->parent->parent->color = RBTREE_RED;
        // 시점을 할아버지로 변경, 할아버지에서 다시 확인
        z = z->parent->parent;

      } else {
        // Case 2: Z가 부모의 왼쪽 자식이라면,
        if (z == z->parent->left) {
          // 부모를 우회전해야겠지
          z = z->parent;
          right_rotate(t, z);
        }

        // Case 3: Z는 부모의 오른쪽 자식이기 때문에
        // 색 지정하고 좌회전해야겠지
        z->parent->color = RBTREE_BLACK;
        z->parent->parent->color = RBTREE_RED;
        left_rotate(t, z->parent->parent);
      }
    }
  }

  // Root 노드는 항상 Black
  t->root->color = RBTREE_BLACK;
}

void rbtree_transplant(rbtree *t, node_t *u, node_t *v) {
  if (u->parent == t->nil) {
    t->root = v;
  } else if (u == u->parent->left) {
    u->parent->left = v;
  } else {
    u->parent->right = v;
  }

  v->parent = u->parent;
}

// Case 1: X의 형제 W가 RED인 경우
// Case 2: X의 형제 W가 RED, W의 두 자식이 모두 BLACK
// Case 3: X의 형제 W가 BLACK, W의 왼쪽 자식이 RED, W의 오른쪽 자식이 BLACK
// Case 4: X의 형제 W가 BLACK, W의 오른쪽 자식이 RED

void rbtree_delete_fixup(rbtree *t, node_t *x) {
  // 삭제하려는 색이 RED라면, 어떠한 속성도 위반하지 않음
  // 하지만 삭제하려는 색이 BLACK이라면, 속성 위반 가능
  while (x != t->root && x->color == RBTREE_BLACK) {
    // 1) 왼쪽 자식일 때
    if (x == x->parent->left){
      // 오른쪽 자식이 형제 노드겠지
      node_t *w = x->parent->right;
      
      // Case 1: 형제 노드 RED
      if (w->color == RBTREE_RED){
        w->color = RBTREE_BLACK;
        x->parent->color = RBTREE_RED;
        left_rotate(t, x->parent);
        w = x->parent->right;
      }

      // Case 2: 형제, 자신 둘 다 BLACK
      // 1) Red n Black: Black으로 변환 후 종료
      // 2) Doubly Black: 다시 while문을 순회
      if (w->left->color == RBTREE_BLACK && w->right->color == RBTREE_BLACK) {
        w->color = RBTREE_RED;
        x = x->parent;

      } else {
        // Case 3: 왼쪽 자식 RED, 오른쪽 자식 BLACK
        if (w->right->color == RBTREE_BLACK) {
          w->left->color = RBTREE_BLACK;
          w->color = RBTREE_RED;
          right_rotate(t, w);
          w = x->parent->right;
        }

        // Case 4: 오른쪽 자식 RED
        w->color = x->parent->color;
        x->parent->color = RBTREE_BLACK;
        w->right->color = RBTREE_BLACK;
        left_rotate(t, x->parent);
        x = t->root;
      }

    // 2) 오른쪽 자식일 때
    } else {
      // 왼쪽 자식이 형제 노드겠지
      node_t *w = x->parent->left;

      // Case 1: 형제 노드 RED
      if (w->color == RBTREE_RED){
        w->color = RBTREE_BLACK;
        x->parent->color = RBTREE_RED;
        right_rotate(t, x->parent);
        w = x->parent->left;
      }

      // Case 2: 형제, 자신 둘 다 BLACK
      // 1) Red n Black: Black으로 변환 후 종료
      // 2) Doubly Black: 다시 while문을 순회
      if (w->right->color == RBTREE_BLACK && w->left->color == RBTREE_BLACK) {
        w->color = RBTREE_RED;
        x = x->parent;

      } else {
        // Case 3: 왼쪽 자식 BLACK, 오른쪽 자식 RED
        if (w->left->color == RBTREE_BLACK) {
          w->right->color = RBTREE_BLACK;
          w->color = RBTREE_RED;
          left_rotate(t, w);
          w = x->parent->left;
        }

        // Case 4: 왼쪽 자식 RED
        w->color = x->parent->color;
        x->parent->color = RBTREE_BLACK;
        w->left->color = RBTREE_BLACK;
        right_rotate(t, x->parent);
        x = t->root;
      }
    }
  }
  x->color = RBTREE_BLACK;
}

int inorder_array(node_t *p, const rbtree *t, key_t *arr, const size_t n, int index) {
  if (p == t->nil) {
    return index;
  }

  index = inorder_array(p->left, t, arr, n, index);

  arr[index] = p->key;
  index++;

  index = inorder_array(p->right, t, arr, n, index);

  return index;
}