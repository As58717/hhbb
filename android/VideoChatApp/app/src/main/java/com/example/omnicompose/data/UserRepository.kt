package com.example.omnicompose.data

import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

class UserRepository {
    private val cache = MutableStateFlow(sampleUsers())

    fun users(): Flow<List<UserProfile>> = cache.asStateFlow()

    suspend fun refreshNearby(city: String) {
        delay(300) // simulate network call
        cache.value = sampleUsers().map { it.copy(city = city) }
    }

    suspend fun getRandomMatch(): UserProfile {
        delay(500)
        return cache.value.shuffled().first()
    }

    private fun sampleUsers(): List<UserProfile> = listOf(
        UserProfile(
            id = "u1",
            name = "西瓜",
            city = "杭州",
            age = 23,
            avatarUrl = "https://placekitten.com/200/200",
            bio = "喜欢唱歌旅行",
            interests = listOf("唱歌", "旅行", "撸猫")
        ),
        UserProfile(
            id = "u2",
            name = "大海",
            city = "上海",
            age = 26,
            avatarUrl = "https://placekitten.com/201/201",
            bio = "摄影师 & 咖啡控",
            interests = listOf("摄影", "咖啡", "骑行")
        ),
        UserProfile(
            id = "u3",
            name = "小七",
            city = "广州",
            age = 25,
            avatarUrl = "https://placekitten.com/202/202",
            bio = "想找个聊得来的你",
            interests = listOf("二次元", "手作", "电影")
        )
    )
}
