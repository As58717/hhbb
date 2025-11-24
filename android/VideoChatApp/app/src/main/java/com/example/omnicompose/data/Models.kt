package com.example.omnicompose.data

data class UserProfile(
    val id: String,
    val name: String,
    val city: String,
    val age: Int,
    val avatarUrl: String,
    val bio: String,
    val interests: List<String>
)

data class CallSession(
    val sessionId: String,
    val callee: UserProfile,
    val isIncoming: Boolean,
    val token: String? = null
)
